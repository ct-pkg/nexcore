/*
 * tests/smoke.c — 骨架冒烟：只依赖 CT 导出的 NexCore.h，验证
 * create → send → queue → QuickJS → outbox → drain → sink 的跨单元调用链，
 * 以及 stat/shutdown/free 的 ABI 契约。
 */
#include <stdio.h>
#include <string.h>

#include "NexCore.h"

struct SinkState {
    int count;
    size_t last_len;
};

static void on_response(void *user, const char *json, size_t len)
{
    struct SinkState *st = user;

    (void)json;
    st->count++;
    st->last_len = len;
}

int main(void)
{
    struct NexCoreConfig cfg;
    struct SinkState sink;
    NexCoreHandle h = 0;
    int64_t v = 0;
    int32_t rc;
    int32_t count = -1;
    const char *req = "{\"id\":1,\"method\":\"app.refresh\",\"params\":{}}";

    memset(&cfg, 0, sizeof(cfg));
    memset(&sink, 0, sizeof(sink));
    cfg.struct_size = (uint32_t)sizeof(cfg);
    cfg.abi_version = NEXCORE_ABI_VERSION;
    cfg.protocol_version = NEXCORE_PROTOCOL_VERSION;
    cfg.default_timeout_ms = NEXCORE_DEFAULT_TIMEOUT_MS;
    cfg.max_message_bytes = NEXCORE_DEFAULT_MAX_MESSAGE_BYTES;
    cfg.max_pending = NEXCORE_DEFAULT_MAX_PENDING;
    cfg.bootstrap_js = "(function(){})";
    cfg.bootstrap_len = strlen(cfg.bootstrap_js);

    printf("config: sizeof=%zu v1=%d min=%d current=%d abi=%d protocol=%d\n",
           sizeof(cfg), (int)NEXCORE_CONFIG_V1_SIZE,
           (int)NEXCORE_CONFIG_MIN_SIZE, (int)NEXCORE_CONFIG_CURRENT_SIZE,
           (int)NEXCORE_ABI_VERSION, (int)NEXCORE_PROTOCOL_VERSION);

    rc = nexcore_create(&cfg, &h);
    printf("create rc=%d handle=%s\n", (int)rc, h != 0 ? "non-null" : "null");
    if (rc != NX_OK || h == 0) return 1;

    rc = nexcore_stat(h, NX_STAT_INBOX_DEPTH, &v);
    printf("stat(after create) rc=%d depth=%lld\n", (int)rc, (long long)v);
    if (rc != NX_OK || v != 0) return 2;

    rc = nexcore_send(h, req, strlen(req));
    printf("send rc=%d\n", (int)rc);
    if (rc != NX_OK) return 3;

    rc = nexcore_stat(h, NX_STAT_INBOX_DEPTH, &v);
    printf("stat(after send) rc=%d depth=%lld\n", (int)rc, (long long)v);
    if (v != 1) return 4;

    rc = nexcore_set_sink(h, &sink, on_response);
    if (rc != NX_OK) return 5;

    rc = nexcore_drain(h, 0, &count);
    printf("drain rc=%d count=%d sink.count=%d\n", (int)rc, (int)count, sink.count);
    if (rc != NX_OK || count != 1 || sink.count != 1) return 6;

    if (nexcore_stat(h, NX_STAT_INBOX_DEPTH, &v) != NX_OK || v != 0) return 7;
    if (nexcore_stat(h, NX_STAT_OUTBOX_DEPTH, &v) != NX_OK || v != 0) return 8;

    /* 统计键冻结：已实现键可读，已分配未实现键返回 NX_E_INTERNAL，未知键返回 NX_E_INVALID_ARGUMENT。 */
    rc = nexcore_stat(h, NX_STAT_LAST_ERROR, &v);
    printf("stat(LAST_ERROR=%d) rc=%d v=%lld\n", (int)NX_STAT_LAST_ERROR, (int)rc, (long long)v);
    if (rc != NX_OK || v != NX_OK) return 9;
    v = -424242;
    rc = nexcore_stat(h, 10, &v);
    printf("stat(reserved 10) rc=%d v=%lld\n", (int)rc, (long long)v);
    if (rc != NX_E_INTERNAL || v != -424242) return 10;
    rc = nexcore_stat(h, NX_STAT_NEXT_FREE, &v);
    printf("stat(next_free=%d) rc=%d\n", (int)NX_STAT_NEXT_FREE, (int)rc);
    if (rc != NX_E_INVALID_ARGUMENT) return 11;

    rc = nexcore_shutdown(h);
    printf("shutdown rc=%d\n", (int)rc);
    if (rc != NX_OK) return 12;

    rc = nexcore_shutdown(h);
    if (rc != NX_OK) return 13;

    rc = nexcore_send(h, req, strlen(req));
    printf("send(after shutdown) rc=%d\n", (int)rc);
    if (rc != NX_E_SHUTDOWN) return 14;

    rc = nexcore_free(h);
    printf("free rc=%d\n", (int)rc);
    if (rc != NX_OK) return 15;

    printf("SMOKE OK\n");
    return 0;
}
