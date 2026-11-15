/*
 * tests/interrupt.c — PH-13 E1/E2：求值预算与宿主中断钩子。
 *
 * 只用公开 ABI。两条断言：
 *   A) default_timeout_ms 到期必须中止 `while(true){}`，$error 文案含 timeout；
 *   B) interrupt_call 返回非 0 必须中止求值，$error 文案含 interrupted。
 * 两条都在 alarm(5) 兜底下跑：没中断成功就挂死，rc=142。
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "NexCore.h"

struct SinkState {
    int count;
    char last[512];
};

static int32_t g_polls;

static int32_t on_interrupt(void *user)
{
    int32_t *polls = user;

    *polls = *polls + 1;
    return *polls >= 5 ? 1 : 0;
}

static void on_response(void *user, const char *json, size_t len)
{
    struct SinkState *st = user;

    st->count++;
    if (len >= sizeof(st->last)) len = sizeof(st->last) - 1;
    memcpy(st->last, json, len);
    st->last[len] = 0;
}

static NexCoreHandle make_handle(struct SinkState *sink, int32_t timeout_ms,
                                 NexCoreInterruptFn fn, void *user)
{
    struct NexCoreConfig cfg;
    NexCoreHandle h = 0;
    int32_t rc;

    memset(&cfg, 0, sizeof(cfg));
    cfg.struct_size = (uint32_t)sizeof(cfg);
    cfg.abi_version = NEXCORE_ABI_VERSION;
    cfg.protocol_version = NEXCORE_PROTOCOL_VERSION;
    cfg.default_timeout_ms = timeout_ms;
    cfg.max_message_bytes = NEXCORE_DEFAULT_MAX_MESSAGE_BYTES;
    cfg.max_pending = NEXCORE_DEFAULT_MAX_PENDING;
    cfg.eval_mode = NEXCORE_EVAL_PROGRAM;
    cfg.interrupt_call = fn;
    cfg.interrupt_user = user;
    rc = nexcore_create(&cfg, &h);
    if (rc != NX_OK || h == 0) {
        printf("create rc=%d\n", (int)rc);
        return 0;
    }
    if (nexcore_set_sink(h, sink, on_response) != NX_OK) {
        nexcore_shutdown(h);
        nexcore_free(h);
        return 0;
    }
    return h;
}

static int run_spin(NexCoreHandle h, struct SinkState *sink, int64_t *last_error)
{
    int32_t rc;
    int32_t count = -1;

    memset(sink, 0, sizeof(*sink));
    rc = nexcore_send(h, "while(true){}", 14);
    if (rc != NX_OK) return 0;
    rc = nexcore_drain(h, 0, &count);
    nexcore_stat(h, NX_STAT_LAST_ERROR, last_error);
    printf("drain rc=%d delivered=%d last_error=%d payload=%s\n", (int)rc,
           sink->count, (int)*last_error, sink->count == 1 ? sink->last : "<none>");
    return rc == NX_OK && sink->count == 1 && *last_error != 0;
}

int main(void)
{
    struct SinkState sink;
    NexCoreHandle h;
    int64_t last_error;

    /* A：预算中断 */
    alarm(5);
    h = make_handle(&sink, 200, 0, 0);
    if (h == 0) return 1;
    if (!run_spin(h, &sink, &last_error)) {
        printf("FAIL: budget did not stop the spin\n");
        return 2;
    }
    if (strstr(sink.last, "timeout") == 0) {
        printf("FAIL: budget payload has no timeout marker\n");
        return 3;
    }
    if (nexcore_shutdown(h) != NX_OK) return 4;
    if (nexcore_free(h) != NX_OK) return 5;

    /* B：宿主回调中断 */
    g_polls = 0;
    h = make_handle(&sink, 0, on_interrupt, &g_polls);
    if (h == 0) return 6;
    if (!run_spin(h, &sink, &last_error)) {
        printf("FAIL: host interrupt did not stop the spin\n");
        return 7;
    }
    if (strstr(sink.last, "interrupted") == 0) {
        printf("FAIL: interrupt payload has no interrupted marker\n");
        return 8;
    }
    alarm(0);
    printf("polls=%d\n", (int)g_polls);
    if (nexcore_shutdown(h) != NX_OK) return 9;
    if (nexcore_free(h) != NX_OK) return 10;

    printf("INTERRUPT OK\n");
    return 0;
}
