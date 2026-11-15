/*
 * hosts/shell/main.c — PH-11 的 `nex` 解释器 MVP。
 *
 * 只消费公开 ABI（build/abi/NexCore.h）：不 import src/ 的单元，不链中间对象。
 * 流程：读脚本 → create（程序模式 + Shell.print）→ send → drain → 打结果 → exit code。
 *
 * 退出码：0 成功；1 脚本错误（库置了 LAST_ERROR）；2 用法或 IO 失败。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "NexCore.h"

/* 与库投递的 $error 前缀逐字节一致（见 src/NexCore.ct push_script_error）。 */
static const char ERROR_PREFIX[] = "{\"code\":\"E_JS_EXCEPTION\"";

static char *read_file(const char *path, size_t *out_len)
{
    FILE *f;
    long size;
    char *buf;
    size_t n;

    f = fopen(path, "rb");
    if (f == 0) return 0;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return 0;
    }
    rewind(f);
    buf = malloc((size_t)size + 1);
    if (buf == 0) {
        fclose(f);
        return 0;
    }
    n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = 0;
    *out_len = n;
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
                         char **out_json, size_t *out_len)
{
    (void)user;
    if (name_len == 11 && memcmp(name, "Shell.print", 11) == 0) {
        print_first_string(args_json, args_len);
    }
    *out_json = malloc(5);
    if (*out_json == 0) return -1;
    memcpy(*out_json, "null", 5);
    *out_len = 4;
    return 0;
}

int main(int argc, char **argv)
{
    static const char NAMES[] = "[\"Shell.print\"]";
    struct NexCoreConfig cfg;
    NexCoreHandle h = 0;
    char *script;
    size_t script_len;
    size_t names_len;
    int32_t rc;
    int32_t delivered;
    int64_t last_error;

    if (argc < 2) {
        fprintf(stderr, "usage: nex <script>\n");
        return 2;
    }
    script = read_file(argv[1], &script_len);
    if (script == 0) {
        fprintf(stderr, "nex: cannot read %s\n", argv[1]);
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
    cfg.native_call = on_native;
    cfg.native_user = 0;
    cfg.native_names = NAMES;
    names_len = strlen(NAMES);
    cfg.native_names_len = names_len;

    rc = nexcore_create(&cfg, &h);
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
    return last_error == 0 ? 0 : 1;
}
