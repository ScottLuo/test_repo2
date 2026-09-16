/*
 * http_service.h - HTTP 服务固件层
 *
 * 启动 HTTP 服务器（端口 80），提供：
 *   - GET /           返回 LED 控制 Web 页面（5 个按钮）
 *   - GET /flash?n=X  触发 LED 在 3 秒内闪烁 N 次，并重置 10s 空闲超时
 *
 * 前置条件：WiFi AP 已启动（esp_wifi_start 成功），AP 静态 IP 已就绪。
 */
#ifndef HTTP_SERVICE_H
#define HTTP_SERVICE_H

#include "esp_err.h"

/* 初始化并启动 HTTP 服务：
 *   - 创建 10s 空闲超时软定时器
 *   - 启动 httpd 服务器（端口 80）
 *   - 注册路由：GET / (HTML 页面)、GET /flash (触发 LED 闪烁)
 * 成功返回 ESP_OK。失败时仅记录日志，不中断程序。 */
esp_err_t http_service_init(void);

#endif /* HTTP_SERVICE_H */
