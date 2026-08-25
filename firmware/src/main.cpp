#include <Arduino.h>
#include "config.h"
#include "display_co5300.h"
#include "touch_cst9220.h"
#include "imu_qmi8658.h"
#include "audio_es8311.h"
#include "pmic_axp2101.h"
#include "ui_manager.h"
#include "network_client.h"
#include "esp_log.h"

static const char *TAG = "MAIN";

// FreeRTOS Task handles
TaskHandle_t lvglTaskHandle = NULL;
TaskHandle_t sensorsTaskHandle = NULL;
TaskHandle_t networkTaskHandle = NULL;

void lvglTask(void *pvParameters) {
    ESP_LOGI(TAG, "LVGL Task started on core %d", xPortGetCoreID());
    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void sensorsTask(void *pvParameters) {
    ESP_LOGI(TAG, "Sensors & Power Task started on core %d", xPortGetCoreID());
    while (1) {
        imu.update();
        pmic.handleButtonPress();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void networkTask(void *pvParameters) {
    ESP_LOGI(TAG, "Network Polling Task started on core %d", xPortGetCoreID());
    while (1) {
        networkClient.taskLoop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void setup() {
    Serial.begin(115200);
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "Starting Waveshare ESP32-S3 Touch AMOLED 2.16 Monitor");
    ESP_LOGI(TAG, "==================================================");

    // 1. Initialize PMIC power rails
    pmic.init();

    // 2. Initialize CO5300 AMOLED display
    if (!display.init()) {
        ESP_LOGE(TAG, "Display initialization failed!");
    }

    // 3. Initialize CST9220 Touch controller
    touch.init();

    // 4. Initialize QMI8658 6-Axis IMU
    imu.init();

    // 5. Initialize ES8311 Audio Codec
    audio.init();

    // 6. Initialize UI Engine (LVGL dual screens)
    ui.init();

    // 7. Initialize Network & Wi-Fi
    networkClient.init();

    // 8. Create FreeRTOS tasks pinned to cores
    xTaskCreatePinnedToCore(lvglTask, "LVGL_Task", 8192, NULL, 5, &lvglTaskHandle, 1);
    xTaskCreatePinnedToCore(sensorsTask, "Sensors_Task", 4096, NULL, 3, &sensorsTaskHandle, 1);
    xTaskCreatePinnedToCore(networkTask, "Network_Task", 8192, NULL, 2, &networkTaskHandle, 0);

    ESP_LOGI(TAG, "All subsystems initialized and FreeRTOS tasks running.");
}

void loop() {
    // Main loop idle; FreeRTOS tasks handle all processing
    vTaskDelay(pdMS_TO_TICKS(1000));
}
