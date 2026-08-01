/* test_harness.h -- a deliberately tiny unit-test harness.
 *
 * Unity, Criterion and CMocka are all fine frameworks, and every one of them
 * would be a submodule, a build-system integration and a licence to explain,
 * in exchange for features this project does not use: mocking, fixtures,
 * parameterised suites. What is actually needed is "assert, count, report a
 * non-zero exit status", which is 60 lines. Keeping it in-tree means the test
 * suite builds with `cc *.c` on any machine with a C compiler and nothing else.
 */

#ifndef CARDIA_TEST_HARNESS_H
#define CARDIA_TEST_HARNESS_H

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static int g_tests_run = 0;
static int g_tests_failed = 0;
static const char *g_current_test = "";

#define TEST(name) static void name(void)

#define RUN_TEST(fn)                                                          \
    do {                                                                      \
        g_current_test = #fn;                                                 \
        int before = g_tests_failed;                                          \
        fn();                                                                 \
        g_tests_run++;                                                        \
        printf("  %-46s %s\n", #fn,                                           \
               (g_tests_failed == before) ? "ok" : "FAILED");                 \
    } while (0)

#define FAILF(fmt, ...)                                                       \
    do {                                                                      \
        g_tests_failed++;                                                     \
        printf("    %s:%d in %s: " fmt "\n", __FILE__, __LINE__,              \
               g_current_test, __VA_ARGS__);                                  \
    } while (0)

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) FAILF("expected %s", #cond);                             \
    } while (0)

#define CHECK_INT_EQ(a, b)                                                    \
    do {                                                                      \
        long long _a = (long long)(a), _b = (long long)(b);                   \
        if (_a != _b) FAILF("%s == %s: got %lld, want %lld", #a, #b, _a, _b);  \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                 \
    do {                                                                      \
        double _a = (double)(a), _b = (double)(b), _t = (double)(tol);        \
        if (!(fabs(_a - _b) <= _t))                                           \
            FAILF("%s ~= %s: got %.9g, want %.9g (tol %.3g)", #a, #b,         \
                  _a, _b, _t);                                                \
    } while (0)

static inline int test_report(const char *suite)
{
    printf("%s: %d tests, %d failures\n", suite, g_tests_run, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}

#endif /* CARDIA_TEST_HARNESS_H */
