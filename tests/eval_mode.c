/*
 * tests/eval_mode.c — PH-11 E1/E2：程序模式求值与表达式模式的兼容。
 *
 * 只用公开 ABI（build/abi/NexCore.h）。两条断言：
 *   1) eval_mode = NEXCORE_EVAL_PROGRAM 时，多语句脚本与 `#!` 脚本都能求值；
 *   2) eval_mode 缺省（0）时，`{id:1}` 仍是对象字面量——表达式模式行为不变。
 */
#include <stdio.h>
#include <string.h>

#include "NexCore.h"

struct SinkState {
    int count;
    char last[256];
};

static void on_response(void *user, const char *json, size_t len)
{
    struct SinkState *st = user;

    st->count++;
    if (len >= sizeof(st->last)) len = sizeof(st->last) - 1;
    memcpy(st->last, json, len);
    st->last[len] = 0;
}

static NexCoreHandle make_handle(struct SinkState *sink, int32_t mode)
{
    struct NexCoreConfig cfg;
    NexCoreHandle h = 0;
    int32_t rc;

    memset(&cfg, 0, sizeof(cfg));
    cfg.struct_size = (uint32_t)sizeof(cfg);
    cfg.abi_version = NEXCORE_ABI_VERSION;
    cfg.protocol_version = NEXCORE_PROTOCOL_VERSION;
    cfg.default_timeout_ms = NEXCORE_DEFAULT_TIMEOUT_MS;
    cfg.max_message_bytes = NEXCORE_DEFAULT_MAX_MESSAGE_BYTES;
    cfg.max_pending = NEXCORE_DEFAULT_MAX_PENDING;
    cfg.eval_mode = mode;
    rc = nexcore_create(&cfg, &h);
    if (rc != NX_OK || h == 0) {
        printf("create rc=%d (mode=%d)\n", (int)rc, (int)mode);
        return 0;
    }
    if (nexcore_set_sink(h, sink, on_response) != NX_OK) {
        nexcore_shutdown(h);
        nexcore_free(h);
        return 0;
    }
    return h;
}

static int ask(NexCoreHandle h, struct SinkState *sink, const char *src, const char *want)
{
    int32_t rc;
    int32_t count = -1;

    memset(sink, 0, sizeof(*sink));
    rc = nexcore_send(h, src, strlen(src));
    if (rc != NX_OK) {
        printf("send rc=%d src=<%.24s>\n", (int)rc, src);
        return 0;
    }
    rc = nexcore_drain(h, 0, &count);
    if (rc != NX_OK) {
        printf("drain rc=%d\n", (int)rc);
        return 0;
    }
    printf("src=<%.20s> -> %s\n", src, sink->count == 1 ? sink->last : "<none>");
    if (sink->count != 1) return 0;
    return strcmp(sink->last, want) == 0 ? 1 : 0;
}

int main(void)
{
    struct SinkState sink;
    NexCoreHandle h;

    /* A：程序模式——多语句 */
    h = make_handle(&sink, NEXCORE_EVAL_PROGRAM);
    if (h == 0) return 1;
    if (!ask(h, &sink, "var a=1; a+1", "2")) {
        printf("FAIL: program mode cannot evaluate statements (expect 2)\n");
        return 2;
    }
    /* B：程序模式——`#!` 首行 */
    if (!ask(h, &sink, "#!/usr/bin/env nex\nvar b=2; b+1", "3")) {
        printf("FAIL: program mode cannot evaluate shebang script (expect 3)\n");
        return 3;
    }
    if (nexcore_shutdown(h) != NX_OK) return 4;
    if (nexcore_free(h) != NX_OK) return 5;

    /* C：表达式模式（缺省）——对象字面量语义必须不变 */
    h = make_handle(&sink, NEXCORE_EVAL_EXPRESSION);
    if (h == 0) return 6;
    if (!ask(h, &sink, "{id:1}", "{\"id\":1}")) {
        printf("FAIL: expression mode no longer returns object literal\n");
        return 7;
    }
    if (!ask(h, &sink, "1+1", "2")) {
        printf("FAIL: expression mode arithmetic regression\n");
        return 8;
    }
    if (nexcore_shutdown(h) != NX_OK) return 9;
    if (nexcore_free(h) != NX_OK) return 10;

    printf("PROGRAM OK\n");
    return 0;
}
