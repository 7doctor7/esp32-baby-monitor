#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <driver/adc.h>
// #include <stdlib.h>
// #include <stdio.h>

#include "ADCSampler.h"
#include "I2SMEMSSampler.h"
#include "I2SSampler.h"
#include "SDCard.h"
// #include "SPIFFS.h"
#include "WAVFileWriter.h"
#include "config.h"

#define ENABLE_DEBUG
#define DEV_DEBUG_LEVEL 0

static const char* TAG = "DEB|";
static const BaseType_t pro_cpu = 0;
static const BaseType_t app_cpu = 1;
int recording = 0;
int voltage = 0;

extern "C" {
void app_main(void);
}

QueueHandle_t serial_queue;
TaskHandle_t print_serial_handler;
TaskHandle_t i2s_adc_handler;

void SerialPrintTask(void* param);
void ListeningADCTask(void* param);
void RecordFromADCTask(void* param);
void record(const char* fname);
static void PrintTasksInfo();
static void PrintADCVoltage();

void SendToPrint(const char* data) {
  xQueueSend(serial_queue, (void*)&data, portMAX_DELAY);
}

void app_main(void) {
  gpio_set_pull_mode(PIN_NUM_MOSI, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode(PIN_NUM_CLK, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode(PIN_NUM_MISO, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode(PIN_NUM_CS, GPIO_PULLUP_ONLY);

  new SDCard("/sdcard", PIN_NUM_MISO, PIN_NUM_MOSI, PIN_NUM_CLK, PIN_NUM_CS);

  uint32_t* dataSerial = (uint32_t*)malloc(512);
  serial_queue = xQueueCreate(100, sizeof(dataSerial));

  xTaskCreatePinnedToCore(SerialPrintTask, "Serial_Task", 8192, NULL, 5, &print_serial_handler, pro_cpu);
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  xTaskCreatePinnedToCore(RecordFromADCTask, "RecordADC_task", 32768, NULL, 6, &i2s_adc_handler, app_cpu);

  vTaskDelete(NULL);
}

void SerialPrintTask(void* params) {
#if defined(ENABLE_DEBUG)
  ESP_LOGI(TAG, "Listening Debug Queue task created!");
#endif

  const char* lReceivedValue;

  while (true) {
    if (xQueueReceive(serial_queue, &(lReceivedValue), portMAX_DELAY) != errQUEUE_EMPTY) {
      ESP_LOGI(TAG, "%s", lReceivedValue);
    }

    taskYIELD();
  }
}

void RecordFromADCTask(void* param) {
#if defined(ENABLE_DEBUG)
  SendToPrint("RecordFromADCTask Created!");
#endif

  char filename[30];
  char fileInfo[50];
  char timeStr[15];

  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC_VOX_CHANNEL, ADC_ATTEN_DB_11);
  vTaskDelay(10000 / portTICK_RATE_MS);
  voltage = 0;

  while (true) {
    voltage = adc1_get_raw(ADC_VOX_CHANNEL);

#if defined(ENABLE_DEBUG)
    // if (voltage >= 2300) {
    PrintADCVoltage();
    vTaskDelay(10 / portTICK_PERIOD_MS);
    // }
#endif

    if (voltage > 2860 && recording == 0) {
      itoa(esp_log_timestamp(), timeStr, 10);
      strcpy(filename, "/sdcard/rec_");
      strcat(filename, timeStr);
      strcat(filename, ".wav");
      strcpy(fileInfo, "File recording: ");
      strcat(fileInfo, filename + 8);
      SendToPrint(fileInfo);
      record(filename);

      SendToPrint("Wait 100ms");
      vTaskDelay(100 / portTICK_PERIOD_MS);
      PrintADCVoltage();
      SendToPrint("Wait 100ms");
      vTaskDelay(100 / portTICK_PERIOD_MS);
      PrintADCVoltage();
      SendToPrint("Wait 100ms");
      vTaskDelay(100 / portTICK_PERIOD_MS);
      PrintTasksInfo();
      vTaskDelay(100 / portTICK_PERIOD_MS);
      PrintADCVoltage();
      SendToPrint("Wait 100ms");
      vTaskDelay(100 / portTICK_PERIOD_MS);
      PrintTasksInfo();
      vTaskDelay(100 / portTICK_PERIOD_MS);
      PrintADCVoltage();
      SendToPrint("Wait 100ms");
      vTaskDelay(100 / portTICK_PERIOD_MS);
      PrintTasksInfo();
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void record(const char* fname) {
#ifdef USE_I2S_MIC_INPUT
  I2SSampler* input = new I2SMEMSSampler(I2S_NUM_0, i2s_mic_pins, i2s_mic_Config);
#else
  I2SSampler* input = new ADCSampler(ADC_UNIT_1, ADC_MIC_CHANNEL, i2s_adc_config);
#endif

  recording = 1;
  int16_t* samples = (int16_t*)malloc(sizeof(int16_t) * 1024);
  uint32_t recordingTimer = esp_log_timestamp();
  const int minRecTime = 4000; // 4 seconds

  FILE* fp = fopen(fname, "wb");

  input->start();

#if defined(ENABLE_DEBUG)
  SendToPrint("====== Recording ========");
#endif

  // create a new wave file writer
  WAVFileWriter* writer = new WAVFileWriter(fp, input->sample_rate());
  // keep writing until the user releases the button
  while (esp_log_timestamp() - recordingTimer < minRecTime + 1000) {
    int samples_read = input->read(samples, 1024);
    writer->write(samples, samples_read);
  }
  // stop the input
  input->stop();
  // and finish the writing
  writer->finish();
  fclose(fp);
  delete writer;
  free(samples);
  recording = 0;
  voltage = 0;
  vTaskResume(i2s_adc_handler);

#if defined(ENABLE_DEBUG)
  SendToPrint("=== Recording Finish! ===");
#endif
}

static void PrintADCVoltage() {
  char info[300];
  char voltage_str[10];
  char recording_str[10];

  itoa(voltage, voltage_str, 10);
  itoa(recording, recording_str, 10);
  strcpy(info, "VOICE LEVEL: ");
  strcat(info, voltage_str);
  strcat(info, " | Rec: ");
  strcat(info, recording_str);
  SendToPrint(info);
}

static void PrintTasksInfo() {
  char ptrTaskList[1000];
  char print[2000];
  vTaskList(ptrTaskList);

  strcpy(print, "\n");
  strcat(print, "********************************************\n");
  strcat(print, "Task            State   Prio    Stack    Num\n");
  strcat(print, "********************************************\n");
  strcat(print, ptrTaskList);
  strcat(print, "********************************************\n");

  SendToPrint(print);
}
