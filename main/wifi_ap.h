/*
 * wifi_ap.h - WiFi SoftAP 固件层
 *
 * 初始化 SoftAP 模式并注册事件处理；将连接状态经 ap_logic 决策后
 * 驱动 LED 指示（通过 led_control）。
 */
#ifndef WIFI_AP_H
#define WIFI_AP_H

#include "esp_err.h"

/* 初始化并启动 SoftAP：
 *   - 初始化 TCP/IP 栈与事件循环
 *   - 配置并启动 WiFi AP（SSID/密码/最大连接数来自 Kconfig）
 *   - 注册事件处理，维护连接计数并驱动 LED
 * 成功返回 ESP_OK，失败返回对应错误码。 */
esp_err_t wifi_ap_init(void);

#endif /* WIFI_AP_H */
