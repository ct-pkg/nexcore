/*
 * tests/pump_bounds.c — PH-10 E1c：泵的两个边界（白盒）。
 *
 * 只测 ABI 观察不到的语义，故直接调 backend 单元：
 *   1) max_jobs > 0 只跑前 N 个 job，其余留在队列；
 *   2) shutdown 的收尾泵把剩余 job 补齐（不静默丢弃）。
 * 用原生 provider 做计数器，避免依赖 eval（eval 自带 pre-pump，会掩盖剩余量）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "js_backend.h"

static int bumps;

static int32_t count_call(void *user, const char *name, size_t name_len,
                          const char *args, size_t args_len,
                          char **out, size_t *out_len)
{
    (void)user;
    (void)name;
    (void)name_len;
    (void)args;
    (void)args_len;
    bumps = bumps + 1;
    *out = (char *)malloc(5);
    if (*out == 0) return -1;
    memcpy(*out, "null", 5);
    *out_len = 4;
    return 0;
}

int main(void)
{
    const char *boot =
        "for(var i=0;i<3;i++){Promise.resolve().then(function(){Tail.bump();});}";
    const char *names = "[\"Tail.bump\"]";
    void *h;
    int32_t ran;

    bumps = 0;
    h = js_backend_init(boot, (int32_t)strlen(boot), count_call, 0,
                        names, (int32_t)strlen(names), 0, 0, 0);
    if (h == 0) {
        printf("FAIL: init\n");
        return 1;
    }
    ran = js_backend_pump(h, 1);
    printf("A: pump(h,1) ran=%d bumps=%d\n", (int)ran, bumps);
    if (ran != 1 || bumps != 1) {
        printf("FAIL: max_jobs 未生效（期望 ran=1 bumps=1）\n");
        js_backend_shutdown(h);
        return 2;
    }
    if (js_backend_shutdown(h) != 0) return 3;
    printf("B: after shutdown bumps=%d\n", bumps);
    if (bumps != 3) {
        printf("FAIL: shutdown 未补齐剩余 job（期望 bumps=3）\n");
        return 4;
    }

    /* 空队列：pump 必须立即返回 0，不阻塞。 */
    bumps = 0;
    h = js_backend_init("var x=1;", 8, 0, 0, 0, 0, 0, 0, 0);
    if (h == 0) {
        printf("FAIL: init(empty)\n");
        return 5;
    }
    ran = js_backend_pump(h, 0);
    printf("C: empty pump ran=%d\n", (int)ran);
    if (ran != 0) {
        printf("FAIL: 空队列 pump 应为 0\n");
        js_backend_shutdown(h);
        return 6;
    }
    if (js_backend_shutdown(h) != 0) return 7;

    printf("BOUNDS OK\n");
    return 0;
}
