/*
 * test_ap_logic.c - ap_logic（SoftAP 连接状态决策）单元测试
 *
 * 验证“连接计数 + LED 状态决策”逻辑，覆盖任务定义的四种行为：
 *   - AP 启动 → IDLE
 *   - 有设备连接 → CONNECTED
 *   - 有设备正在连接（探测）→ CONNECTING
 *   - 全部断开 → 恢复 IDLE
 *   - 多设备同时连接，全部断开后才恢复 IDLE
 */
#include "ap_logic.h"
#include "test_util.h"

/* 初始化：计数为 0，AP 启动后 LED 为 IDLE */
static int test_init_and_ap_start(void)
{
    ap_logic_t logic;
    ap_logic_init(&logic);
    CHECK(ap_logic_connected_count(&logic) == 0);

    led_state_t st = ap_logic_on_ap_start(&logic);
    CHECK(st == LED_STATE_IDLE);
    CHECK(ap_logic_connected_count(&logic) == 0);
    return 0;
}

/* 单设备连接 → CONNECTED；断开 → IDLE */
static int test_single_connect_disconnect(void)
{
    ap_logic_t logic;
    ap_logic_init(&logic);
    ap_logic_on_ap_start(&logic);

    /* 连接 */
    led_state_t st = ap_logic_on_sta_connected(&logic);
    CHECK(st == LED_STATE_CONNECTED);
    CHECK(ap_logic_connected_count(&logic) == 1);

    /* 断开 */
    st = ap_logic_on_sta_disconnected(&logic);
    CHECK(st == LED_STATE_IDLE);
    CHECK(ap_logic_connected_count(&logic) == 0);
    return 0;
}

/* 多设备：连接 2 个，断开 1 个仍 CONNECTED，再断开才 IDLE */
static int test_multiple_connections(void)
{
    ap_logic_t logic;
    ap_logic_init(&logic);
    ap_logic_on_ap_start(&logic);

    ap_logic_on_sta_connected(&logic);
    ap_logic_on_sta_connected(&logic);
    CHECK(ap_logic_connected_count(&logic) == 2);

    led_state_t st = ap_logic_on_sta_disconnected(&logic);
    CHECK(st == LED_STATE_CONNECTED); /* 仍有 1 个连接 */
    CHECK(ap_logic_connected_count(&logic) == 1);

    st = ap_logic_on_sta_disconnected(&logic);
    CHECK(st == LED_STATE_IDLE);      /* 全部断开 */
    CHECK(ap_logic_connected_count(&logic) == 0);
    return 0;
}

/* 探测：无连接时 → CONNECTING；有连接时 → CONNECTED（忽略探测） */
static int test_probe_no_connection(void)
{
    ap_logic_t logic;
    ap_logic_init(&logic);
    ap_logic_on_ap_start(&logic);

    led_state_t st = ap_logic_on_probe(&logic);
    CHECK(st == LED_STATE_CONNECTING);
    /* 探测本身不改变连接计数 */
    CHECK(ap_logic_connected_count(&logic) == 0);
    return 0;
}

static int test_probe_with_connection(void)
{
    ap_logic_t logic;
    ap_logic_init(&logic);
    ap_logic_on_ap_start(&logic);
    ap_logic_on_sta_connected(&logic); /* 已有连接 */

    led_state_t st = ap_logic_on_probe(&logic);
    CHECK(st == LED_STATE_CONNECTED); /* 已有连接，忽略探测 */
    CHECK(ap_logic_connected_count(&logic) == 1);
    return 0;
}

/* 探测宽限超时：无连接 → IDLE；有连接 → CONNECTED */
static int test_probe_timeout(void)
{
    ap_logic_t logic;
    ap_logic_init(&logic);
    ap_logic_on_probe(&logic); /* 进入 CONNECTING，未连接 */

    led_state_t st = ap_logic_on_probe_timeout(&logic);
    CHECK(st == LED_STATE_IDLE); /* 超时未连接，回退 */

    /* 有连接时超时不影响常亮 */
    ap_logic_on_sta_connected(&logic);
    st = ap_logic_on_probe_timeout(&logic);
    CHECK(st == LED_STATE_CONNECTED);
    return 0;
}

/* 完整状态序列：启动 → 探测(快闪) → 连接(常亮) → 断开(等待) */
static int test_full_lifecycle(void)
{
    ap_logic_t logic;
    ap_logic_init(&logic);

    CHECK(ap_logic_on_ap_start(&logic) == LED_STATE_IDLE);
    CHECK(ap_logic_on_probe(&logic) == LED_STATE_CONNECTING);
    CHECK(ap_logic_on_sta_connected(&logic) == LED_STATE_CONNECTED);
    CHECK(ap_logic_on_sta_disconnected(&logic) == LED_STATE_IDLE);
    CHECK(ap_logic_connected_count(&logic) == 0);
    return 0;
}

/* NULL 安全性 */
static int test_null_safety(void)
{
    ap_logic_t logic;
    ap_logic_init(NULL); /* 不应崩溃 */
    CHECK(ap_logic_connected_count(NULL) == 0);
    CHECK(ap_logic_on_ap_start(NULL) == LED_STATE_IDLE);
    CHECK(ap_logic_on_probe(NULL) == LED_STATE_IDLE);
    CHECK(ap_logic_on_sta_connected(NULL) == LED_STATE_CONNECTED);
    CHECK(ap_logic_on_sta_disconnected(NULL) == LED_STATE_IDLE);
    CHECK(ap_logic_on_probe_timeout(NULL) == LED_STATE_IDLE);

    /* 计数不为负 */
    ap_logic_init(&logic);
    ap_logic_on_sta_disconnected(&logic); /* 计数 0 时断开 */
    CHECK(ap_logic_connected_count(&logic) == 0);
    return 0;
}

int main(void)
{
    printf("== ap_logic unit tests ==\n");
    RUN_TEST(test_init_and_ap_start);
    RUN_TEST(test_single_connect_disconnect);
    RUN_TEST(test_multiple_connections);
    RUN_TEST(test_probe_no_connection);
    RUN_TEST(test_probe_with_connection);
    RUN_TEST(test_probe_timeout);
    RUN_TEST(test_full_lifecycle);
    RUN_TEST(test_null_safety);
    return test_summary("ap_logic");
}
