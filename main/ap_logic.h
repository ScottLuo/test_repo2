/*
 * ap_logic.h - SoftAP 连接状态决策逻辑（纯逻辑，可 host 单元测试）
 *
 * 该模块不依赖任何 ESP/FreeRTOS API，只根据事件序列维护“已连接 STA 数”，
 * 并据此决策 LED 应处于哪种状态。WiFi 事件 → 本模块 → LED 状态，链路清晰，
 * 便于对“连接计数 + 状态决策”做单元测试。
 *
 * 设计说明（“连接中快闪”的近似实现）：
 *   ESP8266 RTOS SDK 不暴露 STA 关联(association)过程的中间事件，
 *   因此用 WIFI_EVENT_AP_PROBEREQRECVED（设备探测本 AP）近似“有设备正在连接”。
 *   - 无 STA 连接时收到探测请求 → LED 进入 CONNECTING（快闪）。
 *   - 探测后若在宽限窗口内完成连接（AP_STACONNECTED）→ LED 进入 CONNECTED（常亮）。
 *   - 若宽限窗口内未连接成功（由固件层定时器触发 on_probe_timeout）→ 回退 IDLE。
 */
#ifndef AP_LOGIC_H
#define AP_LOGIC_H

#include "led_fsm.h"

/* SoftAP 连接状态机 */
typedef struct {
    int connected_count; /* 当前已连接（关联成功）的 STA 数量 */
} ap_logic_t;

/* 初始化：连接计数清零 */
void ap_logic_init(ap_logic_t *logic);

/* 查询当前已连接 STA 数量（不为负） */
int ap_logic_connected_count(const ap_logic_t *logic);

/* SoftAP 启动完成：无连接，LED 等待连接 → IDLE */
led_state_t ap_logic_on_ap_start(ap_logic_t *logic);

/* 收到探测请求（有设备正在寻找/连接本 AP）：
 *   - 已有 STA 连接：保持 CONNECTED
 *   - 无 STA 连接：进入 CONNECTING（快闪，提示“有设备在连接”） */
led_state_t ap_logic_on_probe(ap_logic_t *logic);

/* 一个 STA 关联成功：连接数 +1，LED 常亮 → CONNECTED */
led_state_t ap_logic_on_sta_connected(ap_logic_t *logic);

/* 一个 STA 断开：连接数 -1（下限 0）；
 *   - 仍有 STA 连接：保持 CONNECTED
 *   - 全部断开：回退 IDLE */
led_state_t ap_logic_on_sta_disconnected(ap_logic_t *logic);

/* 探测宽限窗口超时仍未连接成功（由固件定时器触发）：
 *   - 无 STA 连接：回退 IDLE
 *   - 已有 STA 连接：保持 CONNECTED
 * 用于避免“设备仅探测但未连接”时 LED 永久停留在快闪。 */
led_state_t ap_logic_on_probe_timeout(ap_logic_t *logic);

#endif /* AP_LOGIC_H */
