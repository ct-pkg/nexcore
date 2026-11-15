/* ⑭ 生成 registry 的行为测试（不是文本比对）：
 *   - 未注册 provider 时必须 NX_E_INTERNAL（不许“假成功”）；
 *   - 注册后 nx_gen_dispatch 必须真的走到 provider（GEN-05：单次派发）；
 *   - adaptor 必须校验 argc（多传/少传都拒绝，且不触达 provider）；
 *   - registry 表里每个方法都必须可经 dispatch 调用成功（GEN-12 行为面）；
 *   - 注销 provider 后回到 NX_E_INTERNAL。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "NexCore.h"
#include "nexcore_registry.h"

static int32_t g_echo_calls;
static int32_t g_ping_calls;
static char g_last_text[64];

static int make_out(const char *text, char **out, size_t *out_len)
{
    size_t n = strlen(text);
    char *copy = (char *)malloc(n + 1);

    if (copy == 0) return NX_E_INTERNAL;
    memcpy(copy, text, n + 1);
    *out = copy;
    *out_len = n;
    return NX_OK;
}

static int32_t provider_echo(const char *text, char **out, size_t *out_len)
{
    g_echo_calls = g_echo_calls + 1;
    if (text == 0) return NX_E_INVALID_ARGUMENT;
    strncpy(g_last_text, text, sizeof(g_last_text) - 1);
    g_last_text[sizeof(g_last_text) - 1] = '\0';
    return make_out(text, out, out_len);
}

static int32_t provider_ping(char **out, size_t *out_len)
{
    g_ping_calls = g_ping_calls + 1;
    return make_out("pong", out, out_len);
}

static const struct nx_gen_provider g_provider = {provider_echo, provider_ping};

static int fail(const char *what, int32_t rc)
{
    printf("GEN BEHAVIOR FAIL: %s (rc=%d, echo=%d, ping=%d)\n",
           what, (int)rc, (int)g_echo_calls, (int)g_ping_calls);
    return 1;
}

int main(void)
{
    const char *echo_argv[1];
    char *out;
    size_t out_len;
    size_t i;
    int32_t rc;

    echo_argv[0] = "hello";
    out = 0;
    out_len = 0;

    /* 1. 未注册：拒绝而不是假成功 */
    rc = nx_gen_dispatch("echo", echo_argv, 1, &out, &out_len);
    printf("dispatch before register rc=%d\n", (int)rc);
    if (rc != NX_E_INTERNAL) return fail("未注册 provider 必须 NX_E_INTERNAL", rc);
    if (g_echo_calls != 0) return fail("未注册时不得触达 provider", rc);

    /* 2. 注册后经 dispatch 真走到 provider */
    if (nx_gen_register_provider(&g_provider) != NX_OK) return fail("register", NX_E_INTERNAL);
    out = 0;
    out_len = 0;
    rc = nx_gen_dispatch("echo", echo_argv, 1, &out, &out_len);
    printf("dispatch echo rc=%d calls=%d out=%.*s\n",
           (int)rc, (int)g_echo_calls, (int)out_len, out ? out : "");
    if (rc != NX_OK) return fail("dispatch echo", rc);
    if (g_echo_calls != 1) return fail("GEN-05：一次 dispatch 必须只派发一次", rc);
    if (strcmp(g_last_text, "hello") != 0) return fail("provider 没收到实参", rc);
    if (out == 0 || out_len != 5 || memcmp(out, "hello", 5) != 0) {
        return fail("返回值没有透传", rc);
    }
    free(out);

    /* 3. 零参数方法：argc = 0，argv 可以是 NULL */
    out = 0;
    out_len = 0;
    rc = nx_gen_dispatch("ping", 0, 0, &out, &out_len);
    printf("dispatch ping rc=%d calls=%d\n", (int)rc, (int)g_ping_calls);
    if (rc != NX_OK || g_ping_calls != 1) return fail("dispatch ping", rc);
    if (out == 0 || out_len != 4 || memcmp(out, "pong", 4) != 0) {
        return fail("ping 返回值没透传", rc);
    }
    free(out);

    /* 4. adaptor 的 argc 校验：不触达 provider */
    rc = nx_gen_dispatch("echo", 0, 0, 0, 0);
    printf("dispatch echo argc=0 rc=%d\n", (int)rc);
    if (rc != NX_E_INVALID_ARGUMENT) return fail("argc 不符必须拒绝", rc);
    if (g_echo_calls != 1) return fail("argc 拒绝路径不得触达 provider", rc);
    rc = nx_gen_dispatch("ping", echo_argv, 1, 0, 0);
    if (rc != NX_E_INVALID_ARGUMENT) return fail("零参方法多传参数必须拒绝", rc);
    if (g_ping_calls != 1) return fail("ping 拒绝路径不得触达 provider", rc);

    /* 5. 未知方法 */
    rc = nx_gen_dispatch("nope", 0, 0, 0, 0);
    printf("dispatch unknown rc=%d\n", (int)rc);
    if (rc != NX_E_INVALID_ARGUMENT) return fail("未知方法必须拒绝", rc);

    /* 6. GEN-12 行为面：表里每个方法都能经 dispatch 调成功，且各只派发一次 */
    if (nx_gen_method_count != 2) return fail("method_count 应为 2", NX_E_INTERNAL);
    for (i = 0; i < nx_gen_method_count; i++) {
        const struct nx_gen_method *m = &nx_gen_methods[i];
        const char *argv[8];
        size_t n;
        int32_t before;

        if (m->invoke == 0) return fail("registry 里的 invoke 为空", NX_E_INTERNAL);
        for (n = 0; n < m->param_count && n < 8; n++) argv[n] = "0";
        before = g_echo_calls + g_ping_calls;
        rc = nx_gen_dispatch(m->name, argv, m->param_count, &out, &out_len);
        printf("dispatch(%s) rc=%d param_count=%d\n",
               m->name, (int)rc, (int)m->param_count);
        if (rc != NX_OK) return fail("表里每个方法都必须可调用", rc);
        if (g_echo_calls + g_ping_calls != before + 1) {
            return fail("GEN-05：每个方法恰好派发一次", rc);
        }
        free(out);
    }

    /* 7. 注销后回到 NX_E_INTERNAL */
    if (nx_gen_register_provider(0) != NX_OK) return fail("unregister", NX_E_INTERNAL);
    out = 0;
    rc = nx_gen_dispatch("echo", echo_argv, 1, &out, &out_len);
    if (rc != NX_E_INTERNAL) return fail("注销后必须 NX_E_INTERNAL", rc);

    printf("GEN REGISTRY BEHAVIOR OK\n");
    return 0;
}
