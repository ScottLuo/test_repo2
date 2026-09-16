/*
 * led_flash.h - N 次闪烁时序（纯逻辑，可 host 单元测试）
 *
 * 封装"N 次闪烁在 T 毫秒内完成"的时序逻辑，通过 HAL 回调操作 LED。
 * 不依赖任何 ESP/FreeRTOS API。
 *
 * 时序模型（以 count=3, total_ms=3000 为例）：
 *   on_ms = 100ms（固定）
 *   off_ms = total_ms / count - on_ms = 1000 - 100 = 900ms
 *   序列：ON(100) → OFF(900) × count 次，总时长 = count × (on_ms + off_ms)
 *
 * 每次调用 advance() 设置一个相位的电平并返回该相位的持续毫秒数；
 * 所有闪烁完成后 advance 返回 0。
 */
#ifndef LED_FLASH_H
#define LED_FLASH_H

#include <stdint.h>
#include <stdbool.h>
#include "led_fsm.h"  /* 复用 led_hal_t 和 led_level_t */

/* 每次"亮"的固定时长（毫秒） */
#define LED_FLASH_ON_MS  100

/* N 次闪烁时序状态 */
typedef struct {
    uint32_t total_ms;      /* 总时长（默认 3000ms） */
    uint32_t on_ms;         /* 每次"亮"的时长（默认 100ms） */
    uint32_t off_ms;        /* 每次"灭"的时长（init/reset 时计算） */
    int      count;         /* 剩余闪烁次数 */
    bool     phase_on;      /* 当前相位：true=下一段为亮 */
    bool     active;        /* 是否处于活跃状态（未完成所有闪烁） */
    const led_hal_t *hal;   /* HAL 回调 */
} led_flash_t;

/* 初始化：设置闪烁次数 N、总时长 T。
 * count: 要闪烁的次数（1~5）
 * total_ms: 总时长（毫秒，默认 3000）
 * hal: GPIO 操作回调（可为 NULL，此时 advance 始终返回 0）
 * 参数非法（count<=0 或 total_ms==0）时 active=false，advance 返回 0。
 * 初始化后调用 advance() 即开始第一次"亮"。 */
void led_flash_init(led_flash_t *flash, const led_hal_t *hal,
                    int count, uint32_t total_ms);

/* 推进一次 GPIO 时序，设置当前相位电平并返回本阶段应持续的毫秒数。
 * - 返回 0 表示所有闪烁已完成（active == false）或参数非法
 * - 非 0 表示当前阶段（亮或灭）的持续时长
 * 每次调用后自动翻转相位；完成一个完整周期（ON+OFF）后 count 减 1，
 * count 减到 0 时 active 置 false。 */
uint32_t led_flash_advance(led_flash_t *flash);

/* 查询是否仍在闪烁（未完成所有 N 次） */
static inline bool led_flash_is_active(const led_flash_t *flash)
{
    return (flash != NULL) && flash->active;
}

/* 重置（可重新设置 count 和 total_ms 后再次使用） */
void led_flash_reset(led_flash_t *flash, int count, uint32_t total_ms);

#endif /* LED_FLASH_H */
