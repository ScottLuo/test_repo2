/*
 * led_control.h - LED 控制固件层（GPIO + FreeRTOS 任务）
 *
 * 薄封装：GPIO 初始化与 LED 任务运行 led_fsm 时序；
 * 状态变更经事件组（event group）线程安全地通知 LED 任务。
 */
#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include "led_fsm.h"

/* 初始化 LED：配置 GPIO 为输出（默认熄灭），创建并启动 LED 任务。
 * 初始状态为 LED_STATE_IDLE（上电进入 AP 模式后等待连接）。 */
void led_control_init(void);

/* 线程安全地设置 LED 目标状态。
 * 与当前状态相同时不会重复通知（led_fsm_set_state 返回 false）。 */
void led_control_set_state(led_state_t state);

#endif /* LED_CONTROL_H */
