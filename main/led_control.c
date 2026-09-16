/*
 * led_control.c - LED 控制固件层实现
 *
 * 设计：
 *  - GPIO：LED 引脚（CONFIG_AP_LED_GPIO，默认 GPIO2，active-low）配置为输出，默认熄灭。
 *  - 线程模型：独立的 led_task 唯一地驱动 led_fsm / led_flash 设置 GPIO；
 *    其他上下文（WiFi 事件回调、HTTP 服务）只通过事件组请求状态变更，避免对 GPIO/状态机的并发访问。
 *  - 运行模型（关键）：led_task 以"当前阶段时长"作为等待超时进行自驱循环，
 *    事件位仅用于"提前唤醒"——因此模式切换时无需等待当前阶段结束即可立即响应。
 *  - 闪烁模式：收到 LED_EV_FLASH_CMD 后切换到 led_flash 驱动，
 *    完成 N 次闪烁后保持熄灭，直到收到 stop_flash 恢复 AP 状态驱动。
 */
#include "led_control.h"

#include "led_flash.h"

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
#define LED_EV_FLASH_CMD     BIT1

/* 事件组句柄（静态，生命周期与任务一致） */
static EventGroupHandle_t s_led_ev_group = NULL;

/* 待应用的目标状态：set_state 写入、led_task 读取，配合事件位串行化 */
static led_state_t s_pending_state = LED_STATE_IDLE;

/* 状态机实例与 HAL 上下文 */
static led_fsm_t s_fsm;

/* 闪烁模式相关 */
static led_flash_t s_flash;
static bool s_flash_mode = false;
static int s_flash_count = 1;

/* 闪烁模式总时长（毫秒） */
#define FLASH_TOTAL_MS  3000

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

/* HAL 回调：把 led_fsm / led_flash 的目标电平落到 GPIO（active-low 直接对应引脚电平） */
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
 * 唯一驱动 led_fsm / led_flash 设置 GPIO 的上下文，因此无需对 fsm 加锁。
 */
static void led_task(void *arg)
{
    (void)arg;

    for (;;) {
        /* 等待事件或超时（超时时间由当前阶段决定） */
        uint32_t wait_ms;

        if (s_flash_mode) {
            /* 闪烁模式：以 led_flash_advance 返回的时长为等待上限 */
            uint32_t dur = led_flash_advance(&s_flash);
            if (dur == 0) {
                /* 闪烁完成：保持熄灭，退出闪烁模式，恢复 AP 状态驱动 */
                s_flash_mode = false;
                wait_ms = 0;
            } else {
                wait_ms = dur;
            }
        } else {
            /* 正常 AP 状态驱动模式 */
            if (led_fsm_set_state(&s_fsm, s_pending_state)) {
                ESP_LOGI(TAG, "LED state -> %s", led_state_name(s_fsm.state));
            }
            uint32_t duration_ms = led_fsm_advance(&s_fsm);
            if (s_fsm.state == LED_STATE_CONNECTED) {
                /* 常亮：无限等待状态变更事件 */
                wait_ms = 0xFFFFFFF;  /* portMAX_DELAY 的数值 */
            } else {
                wait_ms = duration_ms;
            }
        }

        /* 等待事件或超时 */
        EventBits_t bits;
        if (wait_ms == 0xFFFFFFF) {
            /* 无限等待（仅 CONNECTED 非闪烁模式） */
            bits = xEventGroupWaitBits(s_led_ev_group,
                                       LED_EV_STATE_CHANGE | LED_EV_FLASH_CMD,
                                       pdTRUE, pdFALSE, portMAX_DELAY);
        } else {
            bits = xEventGroupWaitBits(s_led_ev_group,
                                       LED_EV_STATE_CHANGE | LED_EV_FLASH_CMD,
                                       pdTRUE, pdFALSE, pdMS_TO_TICKS(wait_ms));
        }

        /* 处理收到的事件 */
        if (bits & LED_EV_FLASH_CMD) {
            /* 进入闪烁模式 */
            s_flash_mode = true;
            led_flash_reset(&s_flash, s_flash_count, FLASH_TOTAL_MS);
            ESP_LOGI(TAG, "flash mode: %d times in %d ms",
                     s_flash_count, FLASH_TOTAL_MS);
        }

        if ((bits & LED_EV_STATE_CHANGE) && s_flash_mode) {
            /* 停止闪烁，恢复 AP 状态驱动 */
            s_flash_mode = false;
            ESP_LOGI(TAG, "flash stopped, resuming AP-driven LED");
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
    s_flash_mode = false;

    /* 初始化 fsm 为 IDLE（相位"亮"，任务启动后会先点亮一次） */
    led_fsm_init(&s_fsm, &s_led_hal, LED_STATE_IDLE);

    /* 初始化 flash 状态（未激活） */
    led_flash_init(&s_flash, &s_led_hal, 1, FLASH_TOTAL_MS);

    /* 创建 LED 任务：优先级 4，栈 2048 字节 */
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

void led_control_start_flash(int count)
{
    if (s_led_ev_group == NULL) {
        return;
    }
    /* 校验 count 范围 */
    if (count < 1 || count > 5) {
        return;
    }
    s_flash_count = count;
    xEventGroupSetBits(s_led_ev_group, LED_EV_FLASH_CMD);
}

void led_control_stop_flash(void)
{
    if (s_led_ev_group == NULL) {
        return;
    }
    /* 置 STATE_CHANGE 事件位唤醒 led_task；
     * led_task 中检测到当前处于 flash_mode 时会退出闪烁模式。 */
    xEventGroupSetBits(s_led_ev_group, LED_EV_STATE_CHANGE);
}
