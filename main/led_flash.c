/*
 * led_flash.c - N 次闪烁时序实现（纯逻辑）
 *
 * 时序模型：
 *   每"闪一下" = ON(on_ms) + OFF(off_ms)
 *   off_ms = total_ms / count - on_ms（若 < 0 则钳位为 0）
 *   总共执行 count 个完整周期后 active=false。
 */
#include "led_flash.h"

/* 内部：计算 off_ms 并钳位到非负 */
static uint32_t calc_off_ms(uint32_t total_ms, int count)
{
    if (count <= 0 || total_ms == 0) {
        return 0;
    }
    uint32_t period = total_ms / (uint32_t)count;
    uint32_t on_ms = LED_FLASH_ON_MS;
    if (period < on_ms) {
        /* 极端情况：每个周期比 on_ms 还短，off 钳位为 0 */
        return 0;
    }
    return period - on_ms;
}

void led_flash_init(led_flash_t *flash, const led_hal_t *hal,
                    int count, uint32_t total_ms)
{
    if (flash == NULL) {
        return;
    }
    flash->hal = hal;
    flash->total_ms = total_ms;
    flash->on_ms = LED_FLASH_ON_MS;
    flash->count = count;
    flash->phase_on = true;
    flash->off_ms = calc_off_ms(total_ms, count);
    /* 参数非法时直接标记完成 */
    flash->active = (count > 0 && total_ms > 0);
}

uint32_t led_flash_advance(led_flash_t *flash)
{
    if (flash == NULL || !flash->active || flash->hal == NULL) {
        return 0;
    }

    uint32_t dur;
    led_level_t level;

    if (flash->phase_on) {
        level = LED_LEVEL_ON;
        dur = flash->on_ms;
    } else {
        level = LED_LEVEL_OFF;
        dur = flash->off_ms;
    }

    /* 设置 LED 电平 */
    flash->hal->set_level(level, flash->hal->ctx);

    /* 翻转相位 */
    flash->phase_on = !flash->phase_on;

    /* 刚完成 OFF 阶段（phase_on 翻转为 true 表示下一段是 ON），
     * 意味着一个完整 ON+OFF 周期结束，count 减 1。 */
    if (flash->phase_on) {
        flash->count--;
        if (flash->count <= 0) {
            flash->active = false;
        }
    }

    return dur;
}

void led_flash_reset(led_flash_t *flash, int count, uint32_t total_ms)
{
    if (flash == NULL) {
        return;
    }
    flash->total_ms = total_ms;
    flash->count = count;
    flash->phase_on = true;
    flash->off_ms = calc_off_ms(total_ms, count);
    flash->active = (count > 0 && total_ms > 0);
}
