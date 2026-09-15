/*
 * test_util.h - 轻量级自包含单元测试框架（无第三方依赖）
 *
 * 用法：
 *   - 每个测试函数返回 0 表示通过，非 0 表示失败（内部用 CHECK 宏提前返回 1）。
 *   - main() 中用 RUN_TEST 逐个执行，最后 test_summary 输出统计并以失败数作为进程退出码。
 */
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdio.h>

static int g_tests_run = 0;
static int g_tests_failed = 0;

/* 在测试函数内部做断言：条件不成立即打印位置并返回 1（判定失败） */
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            printf("    ASSERT FAILED %s:%d: %s\n", __FILE__, __LINE__,  \
                   #cond);                                               \
            return 1;                                                    \
        }                                                                \
    } while (0)

/* 运行一个测试函数 */
#define RUN_TEST(fn)                                                     \
    do {                                                                 \
        g_tests_run++;                                                   \
        printf("  [RUN ] %s\n", #fn);                                    \
        int _r = (fn)();                                                 \
        if (_r != 0) {                                                   \
            g_tests_failed++;                                            \
            printf("  [FAIL] %s\n", #fn);                                \
        } else {                                                         \
            printf("  [ OK ] %s\n", #fn);                                \
        }                                                                \
    } while (0)

/* 输出本套件统计；失败数为 0 时返回 0，否则返回 1 */
static int test_summary(const char *suite)
{
    printf("==================================================\n");
    printf("%s: %d tests, %d failed\n", suite, g_tests_run, g_tests_failed);
    return (g_tests_failed == 0) ? 0 : 1;
}

#endif /* TEST_UTIL_H */
