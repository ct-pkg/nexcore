/*
 * tests/bootstrap.c — bootstrap_js 必须被真正读取并执行。
 *
 * 做法：bootstrap 定义全局 __ok = 42；随后把 "__ok" 当作一条消息 send，
 * 经 drain → QuickJS 求值 → JSON.stringify → sink 收到 "42"。
 * 如果 bootstrap 从未被读取，这条断言必然失败。
 */
#include <stdio.h>
#include <string.h>

#include "NexCore.h"

struct SinkState {
    char last[256];
    int count;
};

static void on_response(void *user, const char *json, size_t len)
{
    struct SinkState *st = user;

    st->count++;
    if (len >= sizeof(st->last)) len = sizeof(st->last) - 1;
    memcpy(st->last, json, len);
    st->last[len] = 0;
}

int main(void)
{
    struct NexCoreConfig cfg;
    struct SinkState sink;
    NexCoreHandle h = 0;
    int32_t rc;
    int32_t count = -1;

    memset(&cfg, 0, sizeof(cfg));
    memset(&sink, 0, sizeof(sink));
    cfg.struct_size = (uint32_t)sizeof(cfg);
    cfg.abi_version = NEXCORE_ABI_VERSION;
    cfg.protocol_version = NEXCORE_PROTOCOL_VERSION;
    cfg.default_timeout_ms = NEXCORE_DEFAULT_TIMEOUT_MS;
    cfg.max_message_bytes = NEXCORE_DEFAULT_MAX_MESSAGE_BYTES;
    cfg.max_pending = NEXCORE_DEFAULT_MAX_PENDING;
    cfg.bootstrap_js = "globalThis.__ok = 42;";
    cfg.bootstrap_len = strlen(cfg.bootstrap_js);

    rc = nexcore_create(&cfg, &h);
    printf("bootstrap create rc=%d\n", (int)rc);
    if (rc != NX_OK || h == 0) return 1;

    if (nexcore_set_sink(h, &sink, on_response) != NX_OK) return 2;
    if (nexcore_send(h, "__ok", 4) != NX_OK) return 3;

    rc = nexcore_drain(h, 0, &count);
    printf("bootstrap drain rc=%d count=%d last=%s\n", (int)rc, (int)count, sink.last);
    if (rc != NX_OK || count != 1) return 4;
    if (sink.count != 1 || strcmp(sink.last, "42") != 0) return 5;

    if (nexcore_shutdown(h) != NX_OK) return 6;
    if (nexcore_free(h) != NX_OK) return 7;

    printf("BOOTSTRAP OK\n");
    return 0;
}
