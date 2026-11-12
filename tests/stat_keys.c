/* ⑥ 统计键契约测试：
 *   - 10..14：已分配未实现 -> NX_E_INTERNAL，且不得写 *out
 *   - 0 / >= 19：未分配 -> NX_E_INVALID_ARGUMENT，且不得写 *out
 *   - 已实现键：NX_OK，且写回值
 */
#include <stdio.h>
#include <string.h>

#include "NexCore.h"

static int32_t g_rc;
static int64_t g_v;

static int fail(const char *what)
{
    printf("STAT FAIL: %s (rc=%d, out=%lld)\n", what, (int)g_rc, (long long)g_v);
    return 1;
}

static int expect_reserved(NexCoreHandle h, int32_t key)
{
    g_v = -424242;
    g_rc = nexcore_stat(h, key, &g_v);
    printf("stat(%2d) rc=%d out=%lld\n", (int)key, (int)g_rc, (long long)g_v);
    if (g_rc != NX_E_INTERNAL) return fail("reserved key must be NX_E_INTERNAL");
    if (g_v != -424242) return fail("reserved key must not write *out");
    return 0;
}

static int expect_invalid(NexCoreHandle h, int32_t key)
{
    g_v = -7;
    g_rc = nexcore_stat(h, key, &g_v);
    printf("stat(%3d) rc=%d out=%lld\n", (int)key, (int)g_rc, (long long)g_v);
    if (g_rc != NX_E_INVALID_ARGUMENT) return fail("unallocated key must be NX_E_INVALID_ARGUMENT");
    if (g_v != -7) return fail("unallocated key must not write *out");
    return 0;
}

int main(void)
{
    struct NexCoreConfig cfg;
    NexCoreHandle h = 0;
    int32_t key;

    memset(&cfg, 0, sizeof(cfg));
    cfg.struct_size = (uint32_t)sizeof(cfg);
    cfg.abi_version = NEXCORE_ABI_VERSION;
    cfg.protocol_version = NEXCORE_PROTOCOL_VERSION;

    g_rc = nexcore_create(&cfg, &h);
    if (g_rc != NX_OK || h == 0) return fail("create");

    for (key = 10; key <= 14; key++) {
        if (expect_reserved(h, key) != 0) return 1;
    }
    if (expect_invalid(h, 0) != 0) return 1;
    if (expect_invalid(h, 19) != 0) return 1;
    if (expect_invalid(h, 20) != 0) return 1;
    if (expect_invalid(h, 1024) != 0) return 1;

    if (nexcore_send(h, "1", 1) != NX_OK) return fail("send before inbox depth");
    g_v = -1;
    g_rc = nexcore_stat(h, NX_STAT_INBOX_DEPTH, &g_v);
    printf("stat( 1) rc=%d out=%lld\n", (int)g_rc, (long long)g_v);
    if (g_rc != NX_OK || g_v != 1) return fail("inbox depth");

    g_v = -1;
    g_rc = nexcore_stat(h, NX_STAT_LAST_ERROR, &g_v);
    printf("stat(18) rc=%d out=%lld\n", (int)g_rc, (long long)g_v);
    if (g_rc != NX_OK) return fail("last_error");

    g_v = -1;
    g_rc = nexcore_stat(h, NX_STAT_JS_TURNS, &g_v);
    printf("stat(15) rc=%d out=%lld\n", (int)g_rc, (long long)g_v);
    if (g_rc != NX_OK || g_v != 0) return fail("js_turns sampled as 0");

    if (nexcore_shutdown(h) != NX_OK) return fail("shutdown");
    if (nexcore_free(h) != NX_OK) return fail("free");

    printf("STAT KEYS OK\n");
    return 0;
}
