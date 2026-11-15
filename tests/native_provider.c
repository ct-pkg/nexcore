/*
 * tests/native_provider.c — PH-10 E3：Provider 注册可用 + 向后兼容。
 *
 * 断言四条：
 *   1) 注册名可从 JS 调到，返回值经 JSON 往返（数字、字符串、无参对象）；
 *   2) 未注册名在 JS 里抛错（不是静默 undefined 调用）；
 *   3) 回调返回非 NX_OK 时 JS 侧抛错；
 *   4) 老 struct_size（== NEXCORE_CONFIG_MIN_SIZE）调用方：create 成功，
 *      且尾部即使填满 0x A5 垃圾也不会被读——JS 侧看不到任何注入的全局。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "NexCore.h"

struct SinkState {
    int count;
    char last[512];
};

static int32_t echo_call(void *user, const char *name, size_t name_len,
                         const char *args, size_t args_len,
                         char **out, size_t *out_len)
{
    (void)user;
    *out = 0;
    *out_len = 0;

    /* name 是注册时的全名（如 "Shell.ping"），不是末段。 */
    if (name_len == 12 && memcmp(name, "Shell.noFail", 12) == 0) return NX_E_INTERNAL;

    if (name_len == 10 && memcmp(name, "Shell.ping", 10) == 0) {
        const char *pong = "{\"pong\":true}";
        *out = (char *)malloc(strlen(pong) + 1);
        strcpy(*out, pong);
        *out_len = strlen(pong);
        return NX_OK;
    }

    /* 取 JSON 数组的第一个元素（本用例只喂标量）。 */
    {
        size_t i = 0;
        size_t start;
        int depth = 0;

        while (i < args_len && args[i] != '[') i++;
        if (i == args_len) return NX_E_INVALID_ARGUMENT;
        i++;
        while (i < args_len && (args[i] == ' ' || args[i] == '\n')) i++;
        start = i;
        depth = 0;
        while (i < args_len) {
            char c = args[i];
            if (c == '"') {
                i++;
                while (i < args_len && args[i] != '"') {
                    if (args[i] == '\\') i++;
                    i++;
                }
                i++;
                continue;
            }
            if (c == '{' || c == '[') depth++;
            if (c == '}' || c == ']') {
                if (depth == 0) break;
                depth--;
            }
            if (c == ',' && depth == 0) break;
            i++;
        }
        if (i == start) return NX_E_INVALID_ARGUMENT;
        *out_len = i - start;
        *out = (char *)malloc(i - start + 1);
        memcpy(*out, args + start, i - start);
        (*out)[i - start] = 0;
        return NX_OK;
    }
}

static void on_response(void *user, const char *json, size_t len)
{
    struct SinkState *st = user;

    st->count++;
    if (len >= sizeof(st->last)) len = sizeof(st->last) - 1;
    memcpy(st->last, json, len);
    st->last[len] = 0;
}

static void fill(struct NexCoreConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->struct_size = (uint32_t)sizeof(*cfg);
    cfg->abi_version = NEXCORE_ABI_VERSION;
    cfg->protocol_version = NEXCORE_PROTOCOL_VERSION;
    cfg->default_timeout_ms = NEXCORE_DEFAULT_TIMEOUT_MS;
    cfg->max_message_bytes = NEXCORE_DEFAULT_MAX_MESSAGE_BYTES;
    cfg->max_pending = NEXCORE_DEFAULT_MAX_PENDING;
    cfg->bootstrap_js = "(function(){})";
    cfg->bootstrap_len = strlen(cfg->bootstrap_js);
}

static int ask(NexCoreHandle h, struct SinkState *sink, const char *expr, const char **out)
{
    int32_t count = -1;

    memset(sink, 0, sizeof(*sink));
    if (nexcore_send(h, expr, strlen(expr)) != NX_OK) return -1;
    if (nexcore_drain(h, 0, &count) != NX_OK) return -1;
    if (sink->count != 1) return -1;
    *out = sink->last;
    return 0;
}

int main(void)
{
    struct NexCoreConfig cfg;
    struct SinkState sink;
    NexCoreHandle h = 0;
    const char *got = 0;
    int32_t rc;

    fill(&cfg);
    cfg.native_call = echo_call;
    cfg.native_user = 0;
    cfg.native_names = "[\"Shell.echo\",\"Shell.ping\",\"Shell.noFail\"]";
    cfg.native_names_len = strlen(cfg.native_names);

    rc = nexcore_create(&cfg, &h);
    printf("create rc=%d\n", (int)rc);
    if (rc != NX_OK || h == 0) return 1;
    if (nexcore_set_sink(h, &sink, on_response) != NX_OK) return 2;

    if (ask(h, &sink, "Shell.echo(7)", &got) != 0 || strcmp(got, "7") != 0) {
        printf("A1 FAIL: Shell.echo(7) -> %s\n", got ? got : "<none>");
        return 3;
    }
    printf("A1: Shell.echo(7) -> %s\n", got);

    if (ask(h, &sink, "Shell.echo(\"hi\")", &got) != 0 || strcmp(got, "\"hi\"") != 0) {
        printf("A2 FAIL: Shell.echo(\"hi\") -> %s\n", got ? got : "<none>");
        return 4;
    }
    printf("A2: Shell.echo(\"hi\") -> %s\n", got);

    if (ask(h, &sink, "Shell.ping()", &got) != 0 || strcmp(got, "{\"pong\":true}") != 0) {
        printf("A3 FAIL: Shell.ping() -> %s\n", got ? got : "<none>");
        return 5;
    }
    printf("A3: Shell.ping() -> %s\n", got);

    if (ask(h, &sink,
            "(function(){try{Shell.nope();return 'no'}catch(e){return 'threw'}})()",
            &got) != 0 || strcmp(got, "\"threw\"") != 0) {
        printf("B FAIL: unregistered name not rejected -> %s\n", got ? got : "<none>");
        return 6;
    }
    printf("B: unregistered name -> %s\n", got);

    if (ask(h, &sink,
            "(function(){try{Shell.noFail();return 'no'}catch(e){return 'threw'}})()",
            &got) != 0 || strcmp(got, "\"threw\"") != 0) {
        printf("C FAIL: non-NX_OK return not surfaced -> %s\n", got ? got : "<none>");
        return 7;
    }
    printf("C: callback error -> %s\n", got);

    if (nexcore_shutdown(h) != NX_OK) return 8;
    if (nexcore_free(h) != NX_OK) return 9;

    /* D：老调用方五档（88/96/104/112/120）。每档精确分配 N 字节，且紧贴 PROT_NONE 页，
     * 任何越界读都会立刻 SIGSEGV——不依赖 ASan，也不依赖「自己块内」的假绿。 */
    {
        static const size_t sizes[5] = { 88, 96, 104, 112, 120 };
        long ps = sysconf(_SC_PAGESIZE);
        int i;

        for (i = 0; i < 5; i++) {
            size_t n = sizes[i];
            size_t page = (size_t)ps;
            unsigned char *base;
            struct NexCoreConfig *c;

            base = (unsigned char *)mmap(0, page * 2, PROT_READ | PROT_WRITE,
                                          MAP_PRIVATE | MAP_ANON, -1, 0);
            if (base == MAP_FAILED) {
                printf("D%zu FAIL: mmap\n", n);
                return 10;
            }
            mprotect(base + page, page, PROT_NONE);
            c = (struct NexCoreConfig *)(base + page - n);
            memset(c, 0, n);
            c->struct_size = (uint32_t)n;
            c->abi_version = NEXCORE_ABI_VERSION;
            c->protocol_version = NEXCORE_PROTOCOL_VERSION;
            c->default_timeout_ms = NEXCORE_DEFAULT_TIMEOUT_MS;
            c->max_message_bytes = NEXCORE_DEFAULT_MAX_MESSAGE_BYTES;
            c->max_pending = NEXCORE_DEFAULT_MAX_PENDING;
            if (n >= (size_t)((char *)&c->native_names_len - (char *)c) + sizeof(c->native_names_len)) {
                c->native_call = echo_call;
                c->native_user = 0;
                c->native_names = "[\"Shell.echo\"]";
                c->native_names_len = strlen(c->native_names);
            }

            h = 0;
            rc = nexcore_create(c, &h);
            printf("D%u: struct_size=%u create rc=%d\n", (unsigned)n, (unsigned)n, (int)rc);
            if (rc != NX_OK || h == 0) {
                munmap(base, page * 2);
                return 11;
            }
            if (nexcore_set_sink(h, &sink, on_response) != NX_OK) {
                munmap(base, page * 2);
                return 12;
            }
            if (ask(h, &sink, "typeof Shell", &got) != 0) {
                printf("D%u FAIL: 无法求值 typeof Shell\n", (unsigned)n);
                munmap(base, page * 2);
                return 13;
            }
            {
                const char *want = (n >= 120) ? "\"object\"" : "\"undefined\"";
                if (strcmp(got, want) != 0) {
                    printf("D%u FAIL: typeof Shell=%s want=%s\n", (unsigned)n, got, want);
                    munmap(base, page * 2);
                    return 14;
                }
                printf("D%u: typeof Shell=%s\n", (unsigned)n, got);
            }
            if (nexcore_shutdown(h) != NX_OK || nexcore_free(h) != NX_OK) {
                munmap(base, page * 2);
                return 15;
            }
            munmap(base, page * 2);
        }
    }

    printf("NATIVE OK\n");
    return 0;
}
