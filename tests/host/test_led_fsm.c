/*
 * test_led_fsm.c - led_fsm（LED 闪烁时序状态机）单元测试
 *
 * 通过 fake HAL 记录每次 set_level 调用的电平，验证：
 *   - IDLE 模式：亮 100ms / 灭 2900ms 交替（周期 3s）
 *   - CONNECTING 模式：亮 100ms / 灭 100ms 快闪
 *   - CONNECTED 模式：保持点亮
 *   - 状态切换：切换后先点亮一次（相位重置）
 *   - NULL 安全性
 */
#include "led_fsm.h"
#include "test_util.h"

/* fake HAL：记录调用序列 */
#define MAX_CALLS 32
static led_level_t s_levels[MAX_CALLS];
static int s_call_count;

static void fake_set_level(led_level_t level, void *ctx)
{
    (void)ctx;
    if (s_call_count < MAX_CALLS) {
        s_levels[s_call_count] = level;
    }
    s_call_count++;
}

static void reset_fake(void)
{
    s_call_count = 0;
}

static const led_hal_t s_fake_hal = {
    .set_level = fake_set_level,
    .ctx = NULL,
};

/* ---- 测试用例 ---- */

/* 初始化后 get_state 返回初始状态 */
static int test_init_state(void)
{
    led_fsm_t fsm;
    led_fsm_init(&fsm, &s_fake_hal, LED_STATE_IDLE);
    CHECK(led_fsm_get_state(&fsm) == LED_STATE_IDLE);

    led_fsm_init(&fsm, &s_fake_hal, LED_STATE_CONNECTING);
    CHECK(led_fsm_get_state(&fsm) == LED_STATE_CONNECTING);

    led_fsm_init(&fsm, &s_fake_hal, LED_STATE_CONNECTED);
    CHECK(led_fsm_get_state(&fsm) == LED_STATE_CONNECTED);
    return 0;
}

/* NULL 安全性 */
static int test_null_safety(void)
{
    led_fsm_t fsm;
    CHECK(led_fsm_get_state(NULL) == LED_STATE_IDLE);
    CHECK(led_fsm_set_state(NULL, LED_STATE_IDLE) == false);
    CHECK(led_fsm_advance(NULL) == 0);

    /* hal 为 NULL */
    led_fsm_init(&fsm, NULL, LED_STATE_IDLE);
    CHECK(led_fsm_advance(&fsm) == 0);
    return 0;
}

/* IDLE 模式：首次 advance 亮 100ms，第二次灭 2900ms，第三次又亮（周期循环） */
static int test_idle_blink(void)
{
    led_fsm_t fsm;
    reset_fake();
    led_fsm_init(&fsm, &s_fake_hal, LED_STATE_IDLE);

    uint32_t d1 = led_fsm_advance(&fsm); /* 亮 */
    CHECK(d1 == LED_IDLE_ON_MS);
    CHECK(s_levels[0] == LED_LEVEL_ON);

    uint32_t d2 = led_fsm_advance(&fsm); /* 灭 */
    CHECK(d2 == LED_IDLE_OFF_MS);
    CHECK(s_levels[1] == LED_LEVEL_OFF);

    uint32_t d3 = led_fsm_advance(&fsm); /* 亮（下一周期） */
    CHECK(d3 == LED_IDLE_ON_MS);
    CHECK(s_levels[2] == LED_LEVEL_ON);
    return 0;
}

/* CONNECTING 模式：亮 100ms / 灭 100ms 快闪 */
static int test_connecting_blink(void)
{
    led_fsm_t fsm;
    reset_fake();
    led_fsm_init(&fsm, &s_fake_hal, LED_STATE_CONNECTING);

    uint32_t d1 = led_fsm_advance(&fsm);
    CHECK(d1 == LED_CONNECTING_ON_MS);
    CHECK(s_levels[0] == LED_LEVEL_ON);

    uint32_t d2 = led_fsm_advance(&fsm);
    CHECK(d2 == LED_CONNECTING_OFF_MS);
    CHECK(s_levels[1] == LED_LEVEL_OFF);

    uint32_t d3 = led_fsm_advance(&fsm);
    CHECK(d3 == LED_CONNECTING_ON_MS);
    CHECK(s_levels[2] == LED_LEVEL_ON);
    return 0;
}

/* CONNECTED 模式：保持点亮，返回 0 */
static int test_connected_always_on(void)
{
    led_fsm_t fsm;
    reset_fake();
    led_fsm_init(&fsm, &s_fake_hal, LED_STATE_CONNECTED);

    uint32_t d1 = led_fsm_advance(&fsm);
    CHECK(d1 == 0);
    CHECK(s_levels[0] == LED_LEVEL_ON);

    /* 多次 advance 都保持点亮 */
    uint32_t d2 = led_fsm_advance(&fsm);
    CHECK(d2 == 0);
    CHECK(s_levels[1] == LED_LEVEL_ON);
    return 0;
}

/* 状态切换：IDLE -> CONNECTING，切换后先点亮一次（相位重置） */
static int test_state_switch_phase_reset(void)
{
    led_fsm_t fsm;
    reset_fake();
    led_fsm_init(&fsm, &s_fake_hal, LED_STATE_IDLE);

    /* 先跑一次 IDLE 让相位翻转为“灭” */
    led_fsm_advance(&fsm); /* 亮, 相位 -> 灭 */
    led_fsm_advance(&fsm); /* 灭, 相位 -> 亮 */

    /* 切换到 CONNECTING：应重置相位为“亮”，下一次 advance 点亮 */
    bool changed = led_fsm_set_state(&fsm, LED_STATE_CONNECTING);
    CHECK(changed == true);
    CHECK(led_fsm_get_state(&fsm) == LED_STATE_CONNECTING);

    uint32_t d = led_fsm_advance(&fsm);
    CHECK(d == LED_CONNECTING_ON_MS); /* 切换后立即点亮 */
    return 0;
}

/* 相同状态 set_state 返回 false */
static int test_same_state_no_change(void)
{
    led_fsm_t fsm;
    led_fsm_init(&fsm, &s_fake_hal, LED_STATE_IDLE);
    CHECK(led_fsm_set_state(&fsm, LED_STATE_IDLE) == false);
    CHECK(led_fsm_get_state(&fsm) == LED_STATE_IDLE);
    return 0;
}

/* CONNECTED -> IDLE 切换后先点亮一次 */
static int test_connected_to_idle(void)
{
    led_fsm_t fsm;
    reset_fake();
    led_fsm_init(&fsm, &s_fake_hal, LED_STATE_CONNECTED);
    led_fsm_advance(&fsm); /* 保持点亮 */

    led_fsm_set_state(&fsm, LED_STATE_IDLE);
    uint32_t d = led_fsm_advance(&fsm);
    CHECK(d == LED_IDLE_ON_MS);  /* 切换后先点亮 */
    CHECK(s_levels[s_call_count - 1] == LED_LEVEL_ON);
    return 0;
}

int main(void)
{
    printf("== led_fsm unit tests ==\n");
    RUN_TEST(test_init_state);
    RUN_TEST(test_null_safety);
    RUN_TEST(test_idle_blink);
    RUN_TEST(test_connecting_blink);
    RUN_TEST(test_connected_always_on);
    RUN_TEST(test_state_switch_phase_reset);
    RUN_TEST(test_same_state_no_change);
    RUN_TEST(test_connected_to_idle);
    return test_summary("led_fsm");
}
