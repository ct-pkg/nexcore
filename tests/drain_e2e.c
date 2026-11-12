/*
 * tests/drain_e2e.c — send → queue → QuickJS → outbox → drain → sink 端到端。
 *
 * 覆盖：
 *   1. 未注册 sink 时 drain 仍消费队列、不崩溃、投递 0 条；
 *   2. 注册 sink 后 drain 逐条回调，顺序与内容正确；
 *   3. drain 后 INBOX/OUTBOX 深度归零，LAST_ERROR 保持 NX_OK。
 */
#include <stdio.h>
#include <string.h>

#include "NexCore.h"

struct SinkState {
    char items[4][256];
    int count;
};

static void on_response(void *user, const char *json, size_t len)
{
    struct SinkState *st = user;
    int slot = st->count;

    if (slot >= 4) return;
    if (len >= sizeof(st->items[slot])) len = sizeof(st->items[slot]) - 1;
    memcpy(st->items[slot], json, len);
    st->items[slot][len] = 0;
    st->count++;
}

static int make_handle(NexCoreHandle *out)
{
    struct NexCoreConfig cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.struct_size = (uint32_t)sizeof(cfg);
    cfg.abi_version = NEXCORE_ABI_VERSION;
    cfg.protocol_version = NEXCORE_PROTOCOL_VERSION;
    cfg.default_timeout_ms = NEXCORE_DEFAULT_TIMEOUT_MS;
    cfg.max_message_bytes = NEXCORE_DEFAULT_MAX_MESSAGE_BYTES;
    cfg.max_pending = NEXCORE_DEFAULT_MAX_PENDING;
    cfg.bootstrap_js = "";
    cfg.bootstrap_len = 0;
    return nexcore_create(&cfg, out);
}

int main(void)
{
    struct SinkState sink;
    NexCoreHandle h = 0;
    int32_t rc;
    int32_t count = -1;
    int64_t v = 0;
    const char *a = "{\"id\":1,\"method\":\"a\"}";
    const char *b = "{\"id\":2,\"method\":\"b\"}";

    memset(&sink, 0, sizeof(sink));
    rc = make_handle(&h);
    printf("drain_e2e create rc=%d\n", (int)rc);
    if (rc != NX_OK || h == 0) return 1;

    /* 1. 未注册 sink：消息被消费但无人接收。 */
    if (nexcore_send(h, a, strlen(a)) != NX_OK) return 2;
    rc = nexcore_drain(h, 0, &count);
    printf("drain(no sink) rc=%d count=%d\n", (int)rc, (int)count);
    if (rc != NX_OK || count != 0) return 3;
    if (nexcore_stat(h, NX_STAT_INBOX_DEPTH, &v) != NX_OK || v != 0) return 4;
    if (nexcore_stat(h, NX_STAT_OUTBOX_DEPTH, &v) != NX_OK || v != 0) return 5;

    /* 2. 注册 sink：逐条投递，顺序与 JSON 内容一致。 */
    if (nexcore_set_sink(h, &sink, on_response) != NX_OK) return 6;
    if (nexcore_send(h, a, strlen(a)) != NX_OK) return 7;
    if (nexcore_send(h, b, strlen(b)) != NX_OK) return 8;

    rc = nexcore_drain(h, 0, &count);
    printf("drain(sink) rc=%d count=%d sink.count=%d [0]=%s [1]=%s\n",
           (int)rc, (int)count, sink.count, sink.items[0], sink.items[1]);
    if (rc != NX_OK || count != 2 || sink.count != 2) return 9;
    if (strcmp(sink.items[0], a) != 0 || strcmp(sink.items[1], b) != 0) return 10;

    /* 3. 排空后深度归零，错误码未污染。 */
    if (nexcore_stat(h, NX_STAT_INBOX_DEPTH, &v) != NX_OK || v != 0) return 11;
    if (nexcore_stat(h, NX_STAT_OUTBOX_DEPTH, &v) != NX_OK || v != 0) return 12;
    if (nexcore_stat(h, NX_STAT_LAST_ERROR, &v) != NX_OK || v != NX_OK) return 13;

    /* 4. 关闭时排空未投递消息：send 后直接 shutdown，sink 仍应收到。 */
    if (nexcore_send(h, a, strlen(a)) != NX_OK) return 14;
    if (nexcore_shutdown(h) != NX_OK) return 15;
    if (sink.count != 3 || strcmp(sink.items[2], a) != 0) return 16;

    if (nexcore_free(h) != NX_OK) return 17;

    printf("DRAIN E2E OK\n");
    return 0;
}
