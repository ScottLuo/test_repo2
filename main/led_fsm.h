/*
 * led_fsm.h - LED 闪烁时序状态机（纯逻辑，可 host 单元测试）
 *
 * 该模块不依赖任何 ESP/FreeRTOS API，通过 HAL 回调注入 GPIO 操作，
 * 因此可在 host 端用 fake HAL 直接对时序逻辑做单元测试。
 *
 * LED 为 active-low（低电平点亮）：LED_LEVEL_ON=0, LED_LEVEL_OFF=1。
 */
#ifndef LED_FSM_H
#define LED_FSM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* LED 引脚目标电平（active-low 语义） */
typedef enum {
    LED_LEVEL_ON  = 0,  /* 引脚低电平 -> 点亮 */
    LED_LEVEL_OFF = 1,  /* 引脚高电平 -> 熄灭 */
} led_level_t;

/* LED 工作状态 */
typedef enum {
    LED_STATE_IDLE      = 0, /* 等待连接：每 3 秒闪一次 */
    LED_STATE_CONNECTING = 1, /* 连接中：快闪 */
    LED_STATE_CONNECTED = 2,  /* 已连接：常亮 */
} led_state_t;

/* HAL 抽象：由调用方（固件或测试）提供具体 GPIO 实现 */
typedef struct {
    void (*set_level)(led_level_t level, void *ctx); /* 设置 LED 引脚电平 */
    void *ctx;                                       /* 透传给回调的上下文 */
} led_hal_t;

/* 状态机内部状态 */
typedef struct {
    led_state_t state;    /* 当前状态 */
    bool phase_on;        /* 闪烁相位：true 表示下一段为“亮” */
    const led_hal_t *hal; /* 指向 HAL 回调 */
} led_fsm_t;

/* 默认闪烁时序（毫秒） */
#define LED_IDLE_ON_MS          100   /* IDLE：亮 100ms */
#define LED_IDLE_OFF_MS         2900  /* IDLE：灭 2900ms（周期 3s） */
#define LED_CONNECTING_ON_MS    100   /* CONNECTING：亮 100ms */
#define LED_CONNECTING_OFF_MS   100   /* CONNECTING：灭 100ms（快闪） */

/* 初始化状态机；initial 为起始状态。
 * 若 initial 非 CONNECTED，相位重置为“亮”，使进入后立即点亮一次。 */
void led_fsm_init(led_fsm_t *fsm, const led_hal_t *hal, led_state_t initial);

/* 查询当前状态 */
led_state_t led_fsm_get_state(const led_fsm_t *fsm);

/* 设置目标状态；返回 true 表示状态实际发生了变化。
 * 切换到非 CONNECTED 状态时，相位重置为“亮”，保证切换后先点亮一次。 */
bool led_fsm_set_state(led_fsm_t *fsm, led_state_t state);

/* 推进一次 GPIO 时序：根据当前状态与相位设置引脚电平，
 * 并返回“本阶段应持续”的毫秒数。调用者据此决定等待时长。
 * - LED_CONNECTED：保持点亮，返回 0（表示等待状态变更，不依赖本返回值）。
 * - LED_IDLE：返回 LED_IDLE_ON_MS 或 LED_IDLE_OFF_MS。
 * - LED_CONNECTING：返回 LED_CONNECTING_ON_MS 或 LED_CONNECTING_OFF_MS。
 * 每次调用后自动翻转相位（CONNECTED 不参与相位翻转）。 */
uint32_t led_fsm_advance(led_fsm_t *fsm);

#endif /* LED_FSM_H */
