/*
 * led_control.c - LED 控制固件层实现
 *
 * 设计：
 *  - GPIO：LED 引脚（CONFIG_AP_LED_GPIO，默认 GPIO2，active-low）配置为输出，默认熄灭。
 *  - 线程模型：独立的 led_task 唯一地驱动 led_fsm 设置 GPIO；
 *    其他上下文（WiFi 事件回调）只通过事件组请求状态变更，避免对 GPIO/状态机的并发访问。
 *  - 运行模型（关键）：led_task 以“当前闪烁阶段时长”作为等待超时进行自驱循环，
 *    事件位（LED_EV_STATE_CHANGE）仅用于“提前唤醒”——因此状态变更时无需等待当前阶段
 *    结束即可立即切换（满足 <500ms 响应要求），且初始 IDLE 状态下无需外部事件即可自主闪烁。
 *    CONNECTED（常亮）阶段则阻塞等待下一次状态变更事件。
 */
#include "led_control.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "led";

/* LED 引脚号（Kconfig: AP_LED_GPIO，默认 GPIO2） */
static const gpio_num_t s_led_gpio = (gpio_num_t)CONFIG_AP_LED_GPIO;

/* 事件组位定义 */
#define LED_EV_STATE_CHANGE  BIT0

/* 事件组句柄（静态，生命周期与任务一致） */
static EventGroupHandle_t s_led_ev_group = NULL;

/* 待应用的目标状态：set_state 写入、led_task 读取，配合事件位串行化 */
static led_state_t s_pending_state = LED_STATE_IDLE;

/* 状态机实例与 HAL 上下文 */
static led_fsm_t s_fsm;

/* 状态名（仅用于日志） */
static const char *led_state_name(led_state_t s)
{
    switch (s) {
    case LED_STATE_CONNECTED:  return "CONNECTED";
    case LED_STATE_CONNECTING: return "CONNECTING";
    case LED_STATE_IDLE:
    default:                   return "IDLE";
    }
}

/* HAL 回调：把 led_fsm 的目标电平落到 GPIO（active-low 直接对应引脚电平） */
static void led_hal_set_level(led_level_t level, void *ctx)
{
    (void)ctx;
    esp_err_t err = gpio_set_level(s_led_gpio, (level == LED_LEVEL_ON) ? 0 : 1);
    if (err != ESP_OK) {
        /* GPIO 写入失败不应中断状态机；仅记录日志 */
        ESP_LOGW(TAG, "gpio_set_level failed: %s", esp_err_to_name(err));
    }
}

static const led_hal_t s_led_hal = {
    .set_level = led_hal_set_level,
    .ctx = NULL,
};

/*
 * led_task：优先级 4（低于 WiFi 驱动的高优先级任务），栈 2048 字节。
 * 唯一驱动 led_fsm 设置 GPIO 的上下文，因此无需对 fsm 加锁。
 */
static void led_task(void *arg)
{
    (void)arg;

    for (;;) {
        /* 应用待处理状态（与当前状态相同时 led_fsm_set_state 返回 false，跳过日志） */
        if (led_fsm_set_state(&s_fsm, s_pending_state)) {
            ESP_LOGI(TAG, "LED state -> %s", led_state_name(s_fsm.state));
        }

        /* 推进一次 GPIO 时序，得到本阶段应持续的毫秒数 */
        uint32_t duration_ms = led_fsm_advance(&s_fsm);

        if (s_fsm.state == LED_STATE_CONNECTED) {
            /* 常亮：保持点亮，阻塞等待下一次状态变更（无限等待） */
            (void)xEventGroupWaitBits(s_led_ev_group, LED_EV_STATE_CHANGE,
                                      pdTRUE, pdTRUE, portMAX_DELAY);
        } else {
            /* 闪烁：以本阶段时长为上限等待；期间状态变更事件会提前唤醒 */
            (void)xEventGroupWaitBits(s_led_ev_group, LED_EV_STATE_CHANGE,
                                      pdTRUE, pdTRUE, pdMS_TO_TICKS(duration_ms));
        }
    }
}

void led_control_init(void)
{
    /* 配置 LED 引脚为输出 */
    esp_err_t err = gpio_set_direction(s_led_gpio, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_set_direction(%d) failed: %s",
                 (int)s_led_gpio, esp_err_to_name(err));
    }

    /* 默认熄灭（active-low：高电平灭） */
    err = gpio_set_level(s_led_gpio, 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "gpio_set_level initial failed: %s", esp_err_to_name(err));
    }

    s_led_ev_group = xEventGroupCreate();
    if (s_led_ev_group == NULL) {
        ESP_LOGE(TAG, "failed to create event group");
        return;
    }

    s_pending_state = LED_STATE_IDLE;

    /* 初始化 fsm 为 IDLE（相位“亮”，任务启动后会先点亮一次） */
    led_fsm_init(&s_fsm, &s_led_hal, LED_STATE_IDLE);

    /* 创建 LED 任务：优先级 4，栈 2048 字 */
    BaseType_t ok = xTaskCreate(led_task, "led_task", 2048, NULL, 4, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create led_task");
    }
}

void led_control_set_state(led_state_t state)
{
    if (s_led_ev_group == NULL) {
        return;
    }
    /* 记录待处理状态并置事件位唤醒 led_task。
     * 同一状态重复调用时 led_task 内 led_fsm_set_state 返回 false，无副作用。 */
    s_pending_state = state;
    xEventGroupSetBits(s_led_ev_group, LED_EV_STATE_CHANGE);
}
