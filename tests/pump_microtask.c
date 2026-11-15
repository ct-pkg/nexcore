/*
 * tests/pump_microtask.c — PH-10 E1：微任务泵的宿主可见性。
 *
 * 只用公开 ABI（build/abi/NexCore.h）。宿主在不新增任何导出函数的前提下，
 * 必须能通过 send + drain 观察到 Promise 回调已执行。
 *
 * 断言两条：
 *   1) bootstrap 里挂的 .then 在 drain 之后生效（hits.length == 1）；
 *   2) 两级链按序执行（n == 2），证明是「泵到空」而不是「只跑一个 job」。
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

static int32_t ask(NexCoreHandle h, struct SinkState *sink, const char *expr, const char **out)
{
    int32_t rc;
    int32_t count = -1;

    memset(sink, 0, sizeof(*sink));
    rc = nexcore_send(h, expr, strlen(expr));
    if (rc != NX_OK) return rc;
    rc = nexcore_drain(h, 0, &count);
    if (rc != NX_OK) return rc;
    if (sink->count != 1) return -1;
    *out = sink->last;
    return NX_OK;
}

int main(void)
{
    struct NexCoreConfig cfg;
    struct SinkState sink;
    NexCoreHandle h = 0;
    const char *got = 0;
    int32_t rc;

    memset(&cfg, 0, sizeof(cfg));
    cfg.struct_size = (uint32_t)sizeof(cfg);
    cfg.abi_version = NEXCORE_ABI_VERSION;
    cfg.protocol_version = NEXCORE_PROTOCOL_VERSION;
    cfg.default_timeout_ms = NEXCORE_DEFAULT_TIMEOUT_MS;
    cfg.max_message_bytes = NEXCORE_DEFAULT_MAX_MESSAGE_BYTES;
    cfg.max_pending = NEXCORE_DEFAULT_MAX_PENDING;
    cfg.bootstrap_js =
        "var hits=0;"
        "Promise.resolve().then(function(){hits=hits+1;});";
    cfg.bootstrap_len = strlen(cfg.bootstrap_js);

    rc = nexcore_create(&cfg, &h);
    printf("create rc=%d\n", (int)rc);
    if (rc != NX_OK || h == 0) return 1;
    if (nexcore_set_sink(h, &sink, on_response) != NX_OK) return 2;

    rc = ask(h, &sink, "hits", &got);
    printf("A: send%sdrain rc=%d hits=%s\n", rc == NX_OK ? "+" : " FAIL ", (int)rc,
           rc == NX_OK ? got : "<none>");
    if (rc != NX_OK || strcmp(got, "1") != 0) {
        printf("FAIL: microtasks not pumped (expect hits=1)\n");
        nexcore_shutdown(h);
        nexcore_free(h);
        return 3;
    }

    if (nexcore_shutdown(h) != NX_OK) return 4;
    if (nexcore_free(h) != NX_OK) return 5;

    /* 第二条：两级链必须都跑，且按序 */
    memset(&cfg, 0, sizeof(cfg));
    cfg.struct_size = (uint32_t)sizeof(cfg);
    cfg.abi_version = NEXCORE_ABI_VERSION;
    cfg.protocol_version = NEXCORE_PROTOCOL_VERSION;
    cfg.default_timeout_ms = NEXCORE_DEFAULT_TIMEOUT_MS;
    cfg.max_message_bytes = NEXCORE_DEFAULT_MAX_MESSAGE_BYTES;
    cfg.max_pending = NEXCORE_DEFAULT_MAX_PENDING;
    cfg.bootstrap_js =
        "var n=0;"
        "Promise.resolve().then(function(){n=n+1;return Promise.resolve();})"
        ".then(function(){n=n+1;});";
    cfg.bootstrap_len = strlen(cfg.bootstrap_js);

    h = 0;
    rc = nexcore_create(&cfg, &h);
    if (rc != NX_OK || h == 0) return 6;
    if (nexcore_set_sink(h, &sink, on_response) != NX_OK) return 7;

    rc = ask(h, &sink, "n", &got);
    printf("B: chain rc=%d n=%s\n", (int)rc, rc == NX_OK ? got : "<none>");
    if (rc != NX_OK || strcmp(got, "2") != 0) {
        printf("FAIL: chained microtasks not drained (expect n=2)\n");
        nexcore_shutdown(h);
        nexcore_free(h);
        return 8;
    }

    if (nexcore_shutdown(h) != NX_OK) return 9;
    if (nexcore_free(h) != NX_OK) return 10;

    /* C：有界性（E1b）。自续链在无界实现里会永久挂死；alarm 兜底给出可判定的退出码。 */
    memset(&cfg, 0, sizeof(cfg));
    cfg.struct_size = (uint32_t)sizeof(cfg);
    cfg.abi_version = NEXCORE_ABI_VERSION;
    cfg.protocol_version = NEXCORE_PROTOCOL_VERSION;
    cfg.default_timeout_ms = NEXCORE_DEFAULT_TIMEOUT_MS;
    cfg.max_message_bytes = NEXCORE_DEFAULT_MAX_MESSAGE_BYTES;
    cfg.max_pending = NEXCORE_DEFAULT_MAX_PENDING;
    cfg.bootstrap_js = "var f=function(){Promise.resolve().then(f);}; f();";
    cfg.bootstrap_len = strlen(cfg.bootstrap_js);

    alarm(5);
    h = 0;
    rc = nexcore_create(&cfg, &h);
    if (rc != NX_OK || h == 0) return 16;
    if (nexcore_set_sink(h, &sink, on_response) != NX_OK) return 17;
    rc = ask(h, &sink, "1+1", &got);
    alarm(0);
    printf("C: bounded self-renewing chain rc=%d val=%s\n", (int)rc, rc == NX_OK ? got : "<none>");
    if (rc != NX_OK || strcmp(got, "2") != 0) {
        printf("FAIL: self-renewing chain did not return (pump unbounded)\n");
        nexcore_shutdown(h);
        nexcore_free(h);
        return 18;
    }
    if (nexcore_shutdown(h) != NX_OK) return 19;
    if (nexcore_free(h) != NX_OK) return 20;

    /* D：job 抛错不得静默（E1d）。下一条消息应失败，且 JS_EXCEPTIONS 有真实采样。 */
    memset(&cfg, 0, sizeof(cfg));
    cfg.struct_size = (uint32_t)sizeof(cfg);
    cfg.abi_version = NEXCORE_ABI_VERSION;
    cfg.protocol_version = NEXCORE_PROTOCOL_VERSION;
    cfg.default_timeout_ms = NEXCORE_DEFAULT_TIMEOUT_MS;
    cfg.max_message_bytes = NEXCORE_DEFAULT_MAX_MESSAGE_BYTES;
    cfg.max_pending = NEXCORE_DEFAULT_MAX_PENDING;
    cfg.bootstrap_js = "Promise.resolve().then(function(){throw new Error('boom');});";
    cfg.bootstrap_len = strlen(cfg.bootstrap_js);

    h = 0;
    rc = nexcore_create(&cfg, &h);
    if (rc != NX_OK || h == 0) return 21;
    if (nexcore_set_sink(h, &sink, on_response) != NX_OK) return 22;
    memset(&sink, 0, sizeof(sink));
    nexcore_send(h, "1+1", 3);
    {
        int32_t count = -1;
        int64_t sev = -1;

        nexcore_drain(h, 0, &count);
        nexcore_stat(h, NX_STAT_JS_EXCEPTIONS, &sev);
        printf("D: job threw -> delivered=%d js_exceptions=%d\n", sink.count, (int)sev);
        /* QuickJS 把 job 内 throw 变成被拒绝的 Promise：消息本身照常返回，
         * 但未处理拒绝必须被计数（否则就是静默吞错）。 */
        if (sink.count != 1 || sev < 1) {
            printf("FAIL: job exception swallowed (delivered=%d, exceptions=%d)\n",
                   sink.count, (int)sev);
            nexcore_shutdown(h);
            nexcore_free(h);
            return 23;
        }
    }
    if (nexcore_shutdown(h) != NX_OK) return 24;
    if (nexcore_free(h) != NX_OK) return 25;

    printf("PUMP OK\n");
    return 0;
}
