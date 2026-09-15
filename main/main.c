/*
 * main.c - 应用入口
 *
 * 启动流程：NVS 初始化 → LED 初始化 → SoftAP 初始化。
 * SoftAP 事件处理在 WiFi 事件循环任务中自动驱动 LED 状态指示。
 */
#include "esp_log.h"
#include "nvs_flash.h"

#include "led_control.h"
#include "wifi_ap.h"

static const char *TAG = "ap_demo";

/* NVS 初始化：若因无空闲页失败，先擦除再重试一次 */
static esp_err_t nvs_flash_init_retry(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS error (%s), erasing and re-initializing...", esp_err_to_name(ret));
        ret = nvs_flash_erase();
        if (ret == ESP_OK) {
            ret = nvs_flash_init();
        }
    }
    return ret;
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " ESP8266 SoftAP LED 状态指示演示程序 ");
    ESP_LOGI(TAG, "========================================");

    /* 1. NVS 初始化（WiFi 驱动依赖 NVS 存储配置） */
    esp_err_t ret = nvs_flash_init_retry();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "NVS initialized");

    /* 2. LED 初始化（启动 LED 任务，初始 IDLE 等待连接） */
    led_control_init();
    ESP_LOGI(TAG, "LED initialized");

    /* 3. SoftAP 初始化并启动；失败则不返回，保持 LED 当前状态便于排查 */
    ret = wifi_ap_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "wifi_ap_init failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "SoftAP running, waiting for station connections...");
}
