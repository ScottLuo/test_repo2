/*
 * http_service.c - HTTP 服务固件层实现
 *
 * 路由：
 *   GET /           → 返回内嵌 HTML 页面（5 个按钮）
 *   GET /flash?n=X  → 触发 LED 在 3 秒内闪烁 N 次，并重置 10s 空闲超时定时器
 *
 * 内存策略：
 *   - HTML 页面为 static const char（ROM 段），不占 RAM。
 *   - 查询参数解析使用栈上固定小缓冲区，handler 内不做动态分配。
 *   - httpd 使用默认配置（端口 80，max_open_sockets=7）。
 *
 * 10s 空闲超时：
 *   使用 FreeRTOS 单次软定时器；每次收到 /flash 时 xTimerReset()；
 *   超时回调调用 led_control_stop_flash() 恢复 AP 状态驱动 LED。
 */
#include "http_service.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "led_control.h"

static const char *TAG = "http_svc";

/* 10s 空闲超时（无新操作则恢复 AP 状态驱动 LED） */
#define IDLE_TIMEOUT_MS   10000

/* HTTP 服务器句柄 */
static httpd_handle_t s_server = NULL;

/* 10s 空闲超时软定时器句柄（单次、不自动重载） */
static TimerHandle_t s_idle_timer = NULL;

/* ---- Web 页面（ROM 段，约 800 字节） ---- */
static const char s_html_page[] =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
    "    <title>ESP8266 LED Control</title>\n"
    "    <style>\n"
    "        body { font-family: sans-serif; text-align: center; padding-top: 40px; }\n"
    "        button { display:block; width:200px; margin:10px auto; padding:15px;\n"
    "                 font-size:18px; cursor:pointer; }\n"
    "    </style>\n"
    "</head>\n"
    "<body>\n"
    "    <h1>ESP8266 LED Control</h1>\n"
    "    <button onclick=\"flash(1)\">闪 1 次</button>\n"
    "    <button onclick=\"flash(2)\">闪 2 次</button>\n"
    "    <button onclick=\"flash(3)\">闪 3 次</button>\n"
    "    <button onclick=\"flash(4)\">闪 4 次</button>\n"
    "    <button onclick=\"flash(5)\">闪 5 次</button>\n"
    "    <script>\n"
    "        function flash(n) {\n"
    "            fetch('/flash?n=' + n);\n"
    "        }\n"
    "    </script>\n"
    "</body>\n"
    "</html>\n";

/* 10s 空闲超时回调：恢复 AP 状态驱动 LED。
 * 运行于软件定时器服务任务上下文，仅置事件位，无阻塞操作。 */
static void idle_timeout_cb(TimerHandle_t timer)
{
    (void)timer;
    ESP_LOGI(TAG, "idle timeout (%d ms), resuming AP-driven LED", IDLE_TIMEOUT_MS);
    led_control_stop_flash();
}

/* 重置 10s 空闲超时定时器（每次收到 /flash 时调用；幂等） */
static void idle_timer_reset(void)
{
    if (s_idle_timer != NULL) {
        xTimerReset(s_idle_timer, 0);
    }
}

/* GET / 处理：返回 HTML 控制页面 */
static esp_err_t handler_get_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    /* buf_len = -1 表示由 httpd 内部调用 strlen() */
    return httpd_resp_send(req, s_html_page, -1);
}

/* GET /flash?n=X 处理：触发 LED 闪烁 N 次并重置空闲超时。
 * n 非法（非数字或不在 1~5 范围）时返回 400。 */
static esp_err_t handler_get_flash(httpd_req_t *req)
{
    /* 获取 URL 查询串长度；无查询参数时直接 400 */
    size_t q_len = httpd_req_get_url_query_len(req);
    if (q_len == 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "missing query 'n'", -1);
    }

    /* 用栈上小缓冲区接收查询串（"n=5" 很短，16 字节足够且有余量） */
    char query[16];
    if ((int)q_len >= (int)sizeof(query)) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "query too long", -1);
    }

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return ESP_FAIL;
    }

    char val[8];
    if (httpd_query_key_value(query, "n", val, sizeof(val)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "missing key 'n'", -1);
    }

    /* 解析 n（简单数字，范围 1~5） */
    int n = atoi(val);
    if (n < 1 || n > 5) {
        httpd_resp_set_status(req, "400 Bad Request");
        char msg[40];
        int mlen = snprintf(msg, sizeof(msg), "invalid n: %s (expect 1-5)", val);
        return httpd_resp_send(req, msg, mlen > 0 ? mlen : 0);
    }

    ESP_LOGI(TAG, "flash command: n=%d", n);
    led_control_start_flash(n);
    idle_timer_reset();

    return httpd_resp_send(req, "ok", -1);
}

/* URI handler 定义 */
static const httpd_uri_t uri_index = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = handler_get_index,
};

static const httpd_uri_t uri_flash = {
    .uri = "/flash",
    .method = HTTP_GET,
    .handler = handler_get_flash,
};

esp_err_t http_service_init(void)
{
    /* 1. 创建 10s 单次软定时器 */
    s_idle_timer = xTimerCreate("http_idle",
                                pdMS_TO_TICKS(IDLE_TIMEOUT_MS),
                                pdFALSE,   /* 不自动重载 */
                                NULL,
                                idle_timeout_cb);
    if (s_idle_timer == NULL) {
        /* 定时器创建失败：LED 闪烁仍可触发，仅 stop_flash 不可用，记录 ERROR 继续 */
        ESP_LOGE(TAG, "failed to create idle timeout timer");
    }

    /* 2. 启动 HTTP 服务器 */
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK || s_server == NULL) {
        ESP_LOGE(TAG, "httpd_start failed: 0x%x", ret);
        /* 回滚定时器（避免泄漏），但 httpd 未启动无其他资源 */
        if (s_idle_timer != NULL) {
            xTimerDelete(s_idle_timer, 0);
            s_idle_timer = NULL;
        }
        return ret;
    }

    /* 3. 注册路由 */
    ret = httpd_register_uri_handler(s_server, &uri_index);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register '/' failed: 0x%x", ret);
        return ret;
    }
    ret = httpd_register_uri_handler(s_server, &uri_flash);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register '/flash' failed: 0x%x", ret);
        return ret;
    }

    ESP_LOGI(TAG, "HTTP service started on port 80");
    return ESP_OK;
}
