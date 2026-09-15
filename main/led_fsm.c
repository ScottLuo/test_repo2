/*
 * led_fsm.c - LED 闪烁时序状态机实现（纯逻辑）
 *
 * 本文件不依赖 ESP/FreeRTOS，仅通过 HAL 回调设置引脚电平，
 * 可在 host 端直接编译并测试。
 */
#include "led_fsm.h"

void led_fsm_init(led_fsm_t *fsm, const led_hal_t *hal, led_state_t initial)
{
    if (fsm == NULL) {
        return;
    }
    fsm->state = initial;
    fsm->hal = hal;
    /* 非常亮状态时，进入后立即点亮一次（相位置为“亮”） */
    fsm->phase_on = (initial != LED_STATE_CONNECTED);
}

led_state_t led_fsm_get_state(const led_fsm_t *fsm)
{
    if (fsm == NULL) {
        return LED_STATE_IDLE;
    }
    return fsm->state;
}

bool led_fsm_set_state(led_fsm_t *fsm, led_state_t state)
{
    if (fsm == NULL || fsm->state == state) {
        return false;
    }
    fsm->state = state;
    /* 切换到闪烁状态时重置相位为“亮”，确保切换后先点亮一次再熄灭 */
    if (state != LED_STATE_CONNECTED) {
        fsm->phase_on = true;
    }
    return true;
}

uint32_t led_fsm_advance(led_fsm_t *fsm)
{
    if (fsm == NULL || fsm->hal == NULL || fsm->hal->set_level == NULL) {
        return 0;
    }

    uint32_t duration;

    switch (fsm->state) {
    case LED_STATE_CONNECTED:
        /* 常亮：保持点亮，不依赖返回时长（由调用者阻塞等待状态变更） */
        fsm->hal->set_level(LED_LEVEL_ON, fsm->hal->ctx);
        return 0;

    case LED_STATE_CONNECTING:
        if (fsm->phase_on) {
            fsm->hal->set_level(LED_LEVEL_ON, fsm->hal->ctx);
            duration = LED_CONNECTING_ON_MS;
        } else {
            fsm->hal->set_level(LED_LEVEL_OFF, fsm->hal->ctx);
            duration = LED_CONNECTING_OFF_MS;
        }
        fsm->phase_on = !fsm->phase_on;
        return duration;

    case LED_STATE_IDLE:
    default:
        if (fsm->phase_on) {
            fsm->hal->set_level(LED_LEVEL_ON, fsm->hal->ctx);
            duration = LED_IDLE_ON_MS;
        } else {
            fsm->hal->set_level(LED_LEVEL_OFF, fsm->hal->ctx);
            duration = LED_IDLE_OFF_MS;
        }
        fsm->phase_on = !fsm->phase_on;
        return duration;
    }
}
