/*
 * ap_logic.c - SoftAP 连接状态决策逻辑实现（纯逻辑）
 *
 * 仅维护连接计数并据此决策 LED 状态，不含任何硬件/RTOS 依赖。
 */
#include "ap_logic.h"

void ap_logic_init(ap_logic_t *logic)
{
    if (logic == NULL) {
        return;
    }
    logic->connected_count = 0;
}

int ap_logic_connected_count(const ap_logic_t *logic)
{
    if (logic == NULL) {
        return 0;
    }
    return logic->connected_count;
}

led_state_t ap_logic_on_ap_start(ap_logic_t *logic)
{
    /* AP 刚启动，必然没有连接：等待连接 → IDLE */
    if (logic != NULL) {
        logic->connected_count = 0;
    }
    return LED_STATE_IDLE;
}

led_state_t ap_logic_on_probe(ap_logic_t *logic)
{
    if (logic == NULL) {
        return LED_STATE_IDLE;
    }
    /* 已有设备连接则忽略探测；否则视为“有设备正在连接”→ 快闪 */
    if (logic->connected_count > 0) {
        return LED_STATE_CONNECTED;
    }
    return LED_STATE_CONNECTING;
}

led_state_t ap_logic_on_sta_connected(ap_logic_t *logic)
{
    if (logic == NULL) {
        return LED_STATE_CONNECTED;
    }
    logic->connected_count++;
    return LED_STATE_CONNECTED;
}

led_state_t ap_logic_on_sta_disconnected(ap_logic_t *logic)
{
    if (logic == NULL) {
        return LED_STATE_IDLE;
    }
    if (logic->connected_count > 0) {
        logic->connected_count--;
    }
    /* 仍有设备连接则常亮；全部断开则回到等待连接 */
    return (logic->connected_count > 0) ? LED_STATE_CONNECTED : LED_STATE_IDLE;
}

led_state_t ap_logic_on_probe_timeout(ap_logic_t *logic)
{
    if (logic == NULL) {
        return LED_STATE_IDLE;
    }
    /* 探测后未连接成功且无连接：回退等待连接；有连接则维持常亮 */
    return (logic->connected_count > 0) ? LED_STATE_CONNECTED : LED_STATE_IDLE;
}
