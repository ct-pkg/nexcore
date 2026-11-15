/*
 * tests/error_surface.c — PH-11 E3：脚本异常必须经 sink 透出，不许在库内被吃掉。
 *
 * 只用公开 ABI。断言三条：
 *   1) `throw new Error("boom")` 之后 sink 恰好收到 1 条消息；
 *   2) 该消息是 $error 形状：含 "code":"E_JS_EXCEPTION"，且 message 里有 boom；
 *   3) stat(LAST_ERROR) 非 0（宿主据此决定进程 exit code）。
 */
#include <stdio.h>
#include <string.h>

#include "NexCore.h"

struct SinkState {
    int count;
    char last[512];
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
    int64_t last_error = 0;

    memset(&cfg, 0, sizeof(cfg));
    cfg.struct_size = (uint32_t)sizeof(cfg);
    cfg.abi_version = NEXCORE_ABI_VERSION;
    cfg.protocol_version = NEXCORE_PROTOCOL_VERSION;
    cfg.default_timeout_ms = NEXCORE_DEFAULT_TIMEOUT_MS;
    cfg.max_message_bytes = NEXCORE_DEFAULT_MAX_MESSAGE_BYTES;
    cfg.max_pending = NEXCORE_DEFAULT_MAX_PENDING;
    cfg.eval_mode = NEXCORE_EVAL_PROGRAM;

    rc = nexcore_create(&cfg, &h);
    if (rc != NX_OK || h == 0) {
        printf("create rc=%d\n", (int)rc);
        return 1;
    }
    if (nexcore_set_sink(h, &sink, on_response) != NX_OK) return 2;

    memset(&sink, 0, sizeof(sink));
    rc = nexcore_send(h, "throw new Error(\"boom\")", 26);
    if (rc != NX_OK) {
        printf("send rc=%d\n", (int)rc);
        return 3;
    }
    rc = nexcore_drain(h, 0, &count);
    nexcore_stat(h, NX_STAT_LAST_ERROR, &last_error);
    printf("drain rc=%d delivered=%d last_error=%d\n", (int)rc, sink.count, (int)last_error);
    printf("payload=%s\n", sink.count == 1 ? sink.last : "<none>");

    if (rc != NX_OK) return 4;
    if (sink.count != 1) {
        printf("FAIL: script error swallowed (expect exactly 1 $error)\n");
        return 5;
    }
    if (strstr(sink.last, "\"code\":\"E_JS_EXCEPTION\"") == 0) {
        printf("FAIL: error payload has no E_JS_EXCEPTION code\n");
        return 6;
    }
    if (strstr(sink.last, "boom") == 0) {
        printf("FAIL: error payload lost the engine message\n");
        return 7;
    }
    if (last_error == 0) {
        printf("FAIL: stat(LAST_ERROR) not set\n");
        return 8;
    }

    if (nexcore_shutdown(h) != NX_OK) return 9;
    if (nexcore_free(h) != NX_OK) return 10;
    printf("ERROR OK\n");
    return 0;
}
