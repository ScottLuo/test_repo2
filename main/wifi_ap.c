/*
 * wifi_ap.c - WiFi SoftAP 固件层实现
 *
 * 事件 → ap_logic（连接计数 + 状态决策）→ led_control（驱动 LED）：
 *   - WIFI_EVENT_AP_START            : AP 启动完成 → IDLE（等待连接）
 *   - WIFI_EVENT_AP_PROBEREQRECVED   : 收到探测请求（有设备在找/连接）→ CONNECTING（快闪）
 *   - WIFI_EVENT_AP_STACONNECTED     : STA 关联成功 → CONNECTED（常亮）
 *   - WIFI_EVENT_AP_STADISCONNECTED  : STA 断开；全部断开 → IDLE
 *
 * 探测宽限定时器：收到探测请求进入 CONNECTING 后，启动一个软定时器；
 * 若窗口内仍未有 STA 连接成功（设备仅探测未连接），定时器触发时把 LED 回退到 IDLE，
 * 避免 LED 永久停留在快闪。STA 连接成功后会停止该定时器。
 */
#include "wifi_ap.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "tcpip_adapter.h"

#include "ap_logic.h"
#include "led_control.h"

static const char *TAG = "wifi_ap";

/* 探测宽限窗口：探测后等待连接的最长时间；超时未连接则回退 IDLE。
 * 取得较大值以覆盖典型 WPA2 握手耗时，同时足够短避免长期误报“连接中”。 */
#define PROBE_GRACE_MS  3000

/* SoftAP 配置（来自 Kconfig） */
#define AP_SSID       CONFIG_AP_WIFI_SSID
#define AP_PASSWORD   CONFIG_AP_WIFI_PASSWORD
#define AP_MAX_CONN   CONFIG_AP_MAX_CONN

/* 连接状态决策上下文 */
static ap_logic_t s_ap_logic;

/* 探测宽限软定时器句柄 */
static TimerHandle_t s_probe_timer = NULL;

/* 定时器回调：探测宽限超时，回退 LED（线程安全，仅置事件位） */
static void probe_grace_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    led_state_t st = ap_logic_on_probe_timeout(&s_ap_logic);
    led_control_set_state(st);
    ESP_LOGI(TAG, "probe grace timeout -> LED %s",
             st == LED_STATE_CONNECTED ? "CONNECTED" : "IDLE");
}

/* 启动/重启探测宽限定时器（幂等：xTimerReset 处理未启动/已运行的两种情况） */
static void probe_grace_timer_start(void)
{
    if (s_probe_timer != NULL) {
        xTimerReset(s_probe_timer, 0);
    }
}

/* 停止探测宽限定时器（连接成功后调用） */
static void probe_grace_timer_stop(void)
{
    if (s_probe_timer != NULL) {
        xTimerStop(s_probe_timer, 0);
    }
}

/* WiFi 事件处理器（运行于 WiFi 事件循环任务上下文） */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;

    switch (event_id) {
    case WIFI_EVENT_AP_START: {
        led_state_t st = ap_logic_on_ap_start(&s_ap_logic);
        led_control_set_state(st);
        ESP_LOGI(TAG, "AP started. SSID:%s", AP_SSID);
        break;
    }

    case WIFI_EVENT_AP_PROBEREQRECVED: {
        /* 有设备在探测/尝试连接本 AP：若当前无连接则进入快闪并启动宽限定时器 */
        led_state_t st = ap_logic_on_probe(&s_ap_logic);
        if (st == LED_STATE_CONNECTING) {
            led_control_set_state(st);
            probe_grace_timer_start();
        }
        break;
    }

    case WIFI_EVENT_AP_STACONNECTED: {
        wifi_event_ap_staconnected_t *ev = (wifi_event_ap_staconnected_t *)event_data;
        led_state_t st = ap_logic_on_sta_connected(&s_ap_logic);
        led_control_set_state(st);
        probe_grace_timer_stop(); /* 连接成功，取消宽限定时器 */
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d, connected=%d",
                 MAC2STR(ev->mac), ev->aid, ap_logic_connected_count(&s_ap_logic));
        break;
    }

    case WIFI_EVENT_AP_STADISCONNECTED: {
        wifi_event_ap_stadisconnected_t *ev = (wifi_event_ap_stadisconnected_t *)event_data;
        led_state_t st = ap_logic_on_sta_disconnected(&s_ap_logic);
        led_control_set_state(st);
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d, connected=%d",
                 MAC2STR(ev->mac), ev->aid, ap_logic_connected_count(&s_ap_logic));
        break;
    }

    default:
        break;
    }
}

esp_err_t wifi_ap_init(void)
{
    ap_logic_init(&s_ap_logic);

    /* 创建探测宽限软定时器（单次、自动删除由调用管理；此处使用一次性定时器） */
    s_probe_timer = xTimerCreate("probe_grace",
                                 pdMS_TO_TICKS(PROBE_GRACE_MS),
                                 pdFALSE,          /* 不自动重载 */
                                 NULL,
                                 probe_grace_timer_cb);
    if (s_probe_timer == NULL) {
        ESP_LOGE(TAG, "failed to create probe grace timer");
        return ESP_FAIL;
    }

    /* 初始化 TCP/IP 栈 */
    tcpip_adapter_init();

    /* 创建默认事件循环（WIFI/IP 事件） */
    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 初始化 WiFi 驱动 */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 注册事件处理 */
    ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_event_handler_register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 配置 SoftAP */
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .password = AP_PASSWORD,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };

    /* 空密码则使用开放网络（便于调试） */
    if (strlen(AP_PASSWORD) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode(AP) failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config(AP) failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SoftAP started. SSID:%s password:%s max_conn:%d",
             AP_SSID, AP_PASSWORD, AP_MAX_CONN);
    return ESP_OK;
}
