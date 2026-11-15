/*
 * hosts/shell/main.c — PH-11/PH-12 的 `nex` 解释器。
 *
 * 只消费公开 ABI（build/abi/NexCore.h）：不 import src/ 的单元，不链中间对象。
 * 用法：nex <script> | nex -e '<code>' | nex -
 * 流程：读脚本 → create（程序模式 + Shell.print + process 快照）→ send → drain → 打结果。
 *
 * 退出码：0 成功；1 脚本错误；2 用法/IO 失败；130 收到 SIGINT。
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "NexCore.h"

/* 库投递的 $error 信封统一以 {"code":"E_ 开头（E_JS_EXCEPTION / E_TIMEOUT /
 * E_INTERRUPTED…，见 src/NexCore.ct push_script_error）。宿主只按信封形状
 * 识别错误，不枚举具体码——具体码的分支留给调用方（protocol.md §4）。 */
static const char ERROR_PREFIX[] = "{\"code\":\"E_";

static volatile sig_atomic_t g_sigint = 0;

static void on_sigint(int sig)
{
    (void)sig;
    g_sigint = 1;
}

/* 库在求值循环里轮询这个回调；返回非 0 即中止当前求值（K-4）。 */
static int32_t poll_interrupt(void *user)
{
    (void)user;
    return g_sigint != 0 ? 1 : 0;
}

extern char **environ;

struct Buf {
    char *p;
    size_t len;
    size_t cap;
};

static int buf_add(struct Buf *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap == 0 ? 256 : b->cap;
        char *p;

        while (cap < b->len + n + 1) cap *= 2;
        p = realloc(b->p, cap);
        if (p == 0) return -1;
        b->p = p;
        b->cap = cap;
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
    return 0;
}

/* 追加一个 JSON 字符串（含两端引号）；JSON 字符串同时是合法的 JS 字面量。 */
static int buf_json_str(struct Buf *b, const char *s)
{
    static const char HEX[] = "0123456789abcdef";
    size_t i;

    if (buf_add(b, "\"", 1) != 0) return -1;
    for (i = 0; s[i] != 0; i++) {
        unsigned char c = (unsigned char)s[i];
        char esc[6];

        if (c == '"' || c == '\\') {
            esc[0] = '\\';
            esc[1] = (char)c;
            if (buf_add(b, esc, 2) != 0) return -1;
        } else if (c < 0x20) {
            esc[0] = '\\';
            esc[1] = 'u';
            esc[2] = '0';
            esc[3] = '0';
            esc[4] = HEX[(c >> 4) & 0xF];
            esc[5] = HEX[c & 0xF];
            if (buf_add(b, esc, 6) != 0) return -1;
        } else {
            if (buf_add(b, (const char *)&c, 1) != 0) return -1;
        }
    }
    return buf_add(b, "\"", 1);
}

static char *read_stream(FILE *f, size_t *out_len)
{
    struct Buf b;
    char chunk[4096];
    size_t n;

    memset(&b, 0, sizeof(b));
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (buf_add(&b, chunk, n) != 0) {
            free(b.p);
            return 0;
        }
    }
    if (b.p == 0) {
        b.p = malloc(1);
        if (b.p == 0) return 0;
        b.p[0] = 0;
    }
    *out_len = b.len;
    return b.p;
}

static char *read_file(const char *path, size_t *out_len)
{
    FILE *f;
    char *buf;

    if (strcmp(path, "-") == 0) return read_stream(stdin, out_len);
    f = fopen(path, "rb");
    if (f == 0) return 0;
    buf = read_stream(f, out_len);
    fclose(f);
    return buf;
}

static int is_error_payload(const char *json, size_t len)
{
    if (len < sizeof(ERROR_PREFIX) - 1) return 0;
    return memcmp(json, ERROR_PREFIX, sizeof(ERROR_PREFIX) - 1) == 0;
}

static void on_sink(void *user, const char *json, size_t len)
{
    (void)user;
    if (is_error_payload(json, len)) {
        fwrite(json, 1, len, stderr);
        fputc('\n', stderr);
        return;
    }
    /* 程序没有完成值时库给 "null"：不是脚本输出，不打。 */
    if (len == 4 && memcmp(json, "null", 4) == 0) return;
    fwrite(json, 1, len, stdout);
    fputc('\n', stdout);
}

/* 从 JSON 数组里取第一个字符串元素并原样打印（Shell.print 的 MVP 语义）。 */
static void print_first_string(const char *args, size_t len)
{
    size_t i;
    size_t start;

    i = 0;
    while (i < len && args[i] != '"') i++;
    if (i >= len) return;
    start = i + 1;
    i = start;
    while (i < len) {
        if (args[i] == '\\' && i + 1 < len) {
            i += 2;
            continue;
        }
        if (args[i] == '"') break;
        i++;
    }
    fwrite(args + start, 1, i - start, stdout);
    fputc('\n', stdout);
}

static int32_t on_native(void *user, const char *name, size_t name_len,
                         const char *args_json, size_t args_len,
                         char **out_json, size_t *out_len);

/*
 * Shell.spawn(cmd)：同步执行 `/bin/sh -c cmd`，返回 {"code":N,"stdout":"..."}。
 * 这是宿主能力，不进 ABI：通过 PH-10 的 Provider 尾字段注册（D4）。
 * 异步/流式版本需要事件回投的新 ABI，登记 K-6，本 phase 不做。
 */
static int32_t spawn_json(const char *cmd, char **out_json, size_t *out_len)
{
    FILE *p;
    struct Buf outb;
    struct Buf b;
    char chunk[4096];
    char num[32];
    size_t n;
    int status;
    int code;

    p = popen(cmd, "r");
    if (p == 0) return -1;
    memset(&outb, 0, sizeof(outb));
    while ((n = fread(chunk, 1, sizeof(chunk), p)) > 0) {
        if (buf_add(&outb, chunk, n) != 0) {
            pclose(p);
            free(outb.p);
            return -1;
        }
    }
    status = pclose(p);
    code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    memset(&b, 0, sizeof(b));
    snprintf(num, sizeof(num), "%d", code);
    if (buf_add(&b, "{\"code\":", 8) != 0 ||
        buf_add(&b, num, strlen(num)) != 0 ||
        buf_add(&b, ",\"stdout\":", 10) != 0 ||
        buf_json_str(&b, outb.p != 0 ? outb.p : "") != 0 ||
        buf_add(&b, "}", 1) != 0) {
        free(outb.p);
        free(b.p);
        return -1;
    }
    free(outb.p);
    *out_json = b.p;
    *out_len = b.len;
    return 0;
}

/* 取 JSON 数组第一个字符串的原文（MVP 不解转义）；非字符串参数直接打印其字面量。 */
static char *first_arg_string(const char *args, size_t len)
{
    size_t i;
    size_t start;
    char *out;

    i = 0;
    while (i < len && args[i] != '"') i++;
    if (i >= len) return 0;
    start = i + 1;
    i = start;
    while (i < len) {
        if (args[i] == '\\' && i + 1 < len) {
            i += 2;
            continue;
        }
        if (args[i] == '"') break;
        i++;
    }
    out = malloc(i - start + 1);
    if (out == 0) return 0;
    memcpy(out, args + start, i - start);
    out[i - start] = 0;
    return out;
}

static int32_t on_native(void *user, const char *name, size_t name_len,
                         const char *args_json, size_t args_len,
                         char **out_json, size_t *out_len)
{
    (void)user;
    if (name_len == 11 && memcmp(name, "Shell.print", 11) == 0) {
        print_first_string(args_json, args_len);
    } else if (name_len == 11 && memcmp(name, "Shell.spawn", 11) == 0) {
        char *cmd = first_arg_string(args_json, args_len);
        int32_t rc;

        if (cmd == 0) return -1;
        rc = spawn_json(cmd, out_json, out_len);
        free(cmd);
        return rc;
    }
    *out_json = malloc(5);
    if (*out_json == 0) return -1;
    memcpy(*out_json, "null", 5);
    *out_len = 4;
    return 0;
}

/*
 * 注入 process 快照（argv/env）。argv[0] 固定为 "nex"，argv[1] 是脚本名或 "-e"，
 * 其余原样透传；env 取自启动时的 environ，脚本内修改不回写宿主（K-5）。
 */
static char *build_bootstrap(int argc, char **argv, const char *script_name)
{
    static const char HEAD[] = "globalThis.process=Object.freeze({argv:Object.freeze([";
    static const char MID[] = "]),env:Object.freeze({";
    static const char TAIL[] = "})});";
    struct Buf b;
    int i;
    int first_env;

    memset(&b, 0, sizeof(b));
    if (buf_add(&b, HEAD, sizeof(HEAD) - 1) != 0) goto fail;
    if (buf_json_str(&b, "nex") != 0) goto fail;
    if (buf_add(&b, ",", 1) != 0) goto fail;
    if (buf_json_str(&b, script_name) != 0) goto fail;
    for (i = 2; i < argc; i++) {
        if (buf_add(&b, ",", 1) != 0) goto fail;
        if (buf_json_str(&b, argv[i]) != 0) goto fail;
    }
    if (buf_add(&b, MID, sizeof(MID) - 1) != 0) goto fail;
    first_env = 1;
    if (environ != 0) {
        for (i = 0; environ[i] != 0; i++) {
            const char *eq = strchr(environ[i], '=');
            char *key;
            size_t klen;

            if (eq == 0) continue;
            klen = (size_t)(eq - environ[i]);
            key = malloc(klen + 1);
            if (key == 0) goto fail;
            memcpy(key, environ[i], klen);
            key[klen] = 0;
            if (!first_env && buf_add(&b, ",", 1) != 0) {
                free(key);
                goto fail;
            }
            if (buf_json_str(&b, key) != 0 || buf_add(&b, ":", 1) != 0 ||
                buf_json_str(&b, eq + 1) != 0) {
                free(key);
                goto fail;
            }
            free(key);
            first_env = 0;
        }
    }
    if (buf_add(&b, TAIL, sizeof(TAIL) - 1) != 0) goto fail;
    return b.p;

fail:
    free(b.p);
    return 0;
}

int main(int argc, char **argv)
{
    static const char NAMES[] = "[\"Shell.print\",\"Shell.spawn\"]";
    struct NexCoreConfig cfg;
    NexCoreHandle h = 0;
    char *script;
    char *bootstrap;
    const char *script_name;
    const char *inline_code;
    size_t script_len;
    size_t names_len;
    int32_t rc;
    int32_t delivered;
    int64_t last_error;

    if (argc < 2) {
        fprintf(stderr, "usage: nex <script> | nex -e '<code>' | nex -\n");
        return 2;
    }
    inline_code = 0;
    script_name = 0;
    if (strcmp(argv[1], "-e") == 0) {
        if (argc < 3) {
            fprintf(stderr, "nex: -e requires code\n");
            return 2;
        }
        inline_code = argv[2];
        script_name = "-e";
        script = malloc(strlen(inline_code) + 1);
        if (script == 0) return 2;
        memcpy(script, inline_code, strlen(inline_code) + 1);
        script_len = strlen(inline_code);
    } else {
        script_name = argv[1];
        script = read_file(argv[1], &script_len);
        if (script == 0) {
            fprintf(stderr, "nex: cannot read %s\n", argv[1]);
            return 2;
        }
    }

    bootstrap = build_bootstrap(argc, argv, script_name);
    if (bootstrap == 0) {
        fprintf(stderr, "nex: cannot build process snapshot\n");
        free(script);
        return 2;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.struct_size = (uint32_t)sizeof(cfg);
    cfg.abi_version = NEXCORE_ABI_VERSION;
    cfg.protocol_version = NEXCORE_PROTOCOL_VERSION;
    cfg.default_timeout_ms = NEXCORE_DEFAULT_TIMEOUT_MS;
    cfg.max_message_bytes = NEXCORE_DEFAULT_MAX_MESSAGE_BYTES;
    cfg.max_pending = NEXCORE_DEFAULT_MAX_PENDING;
    cfg.eval_mode = NEXCORE_EVAL_PROGRAM;
    /*
     * 预算默认不限（0）：本地脚本被 5s 掐断会很意外；需要上限时用 NEX_TIMEOUT_MS。
     * 中断钩子始终接线，Ctrl-C 才能停住长求值。
     */
    {
        const char *timeout_env = getenv("NEX_TIMEOUT_MS");

        cfg.default_timeout_ms = timeout_env != 0 ? (uint32_t)strtoul(timeout_env, 0, 10) : 0;
    }
    cfg.interrupt_call = poll_interrupt;
    cfg.interrupt_user = 0;
    cfg.bootstrap_js = bootstrap;
    cfg.bootstrap_len = strlen(bootstrap);
    cfg.native_call = on_native;
    cfg.native_user = 0;
    cfg.native_names = NAMES;
    names_len = strlen(NAMES);
    cfg.native_names_len = names_len;

    signal(SIGINT, on_sigint);

    rc = nexcore_create(&cfg, &h);
    free(bootstrap);
    if (rc != NX_OK || h == 0) {
        fprintf(stderr, "nex: create failed rc=%d\n", (int)rc);
        free(script);
        return 2;
    }
    if (nexcore_set_sink(h, 0, on_sink) != NX_OK) {
        fprintf(stderr, "nex: set_sink failed\n");
        nexcore_shutdown(h);
        nexcore_free(h);
        free(script);
        return 2;
    }

    rc = nexcore_send(h, script, script_len);
    free(script);
    if (rc != NX_OK) {
        fprintf(stderr, "nex: send failed rc=%d\n", (int)rc);
        nexcore_shutdown(h);
        nexcore_free(h);
        return 2;
    }
    delivered = 0;
    rc = nexcore_drain(h, 0, &delivered);
    if (rc != NX_OK) {
        fprintf(stderr, "nex: drain failed rc=%d\n", (int)rc);
        nexcore_shutdown(h);
        nexcore_free(h);
        return 2;
    }

    last_error = 0;
    nexcore_stat(h, NX_STAT_LAST_ERROR, &last_error);
    nexcore_shutdown(h);
    nexcore_free(h);
    if (g_sigint != 0) return 130;
    return last_error == 0 ? 0 : 1;
}
