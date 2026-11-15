/* ⑭ JS 生成物可加载：用 QuickJS 以 module 方式求值生成的 .js。
 * 没有这个，生成的 JS 只能是“看起来对”；本测试只判 rc：语法/顶层求值失败即红。
 */
#include <stdio.h>
#include <stdlib.h>

#include "quickjs.h"

int main(int argc, char **argv)
{
    FILE *f;
    long len;
    char *buf;
    JSRuntime *rt;
    JSContext *ctx;
    JSValue v;

    if (argc != 2) {
        printf("usage: js_module_load <file.js>\n");
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (f == 0) {
        printf("JS LOAD FAIL: 打不开 %s\n", argv[1]);
        return 2;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 2; }
    len = ftell(f);
    if (len < 0) { fclose(f); return 2; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 2; }
    buf = malloc((size_t)len + 1);
    if (buf == 0) { fclose(f); return 2; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(buf);
        return 2;
    }
    fclose(f);
    buf[len] = 0;

    rt = JS_NewRuntime();
    if (rt == 0) { free(buf); return 2; }
    ctx = JS_NewContext(rt);
    if (ctx == 0) { JS_FreeRuntime(rt); free(buf); return 2; }

    v = JS_Eval(ctx, buf, (size_t)len, argv[1], JS_EVAL_TYPE_MODULE);
    if (JS_IsException(v)) {
        JSValue exc = JS_GetException(ctx);
        const char *msg = JS_ToCString(ctx, exc);
        printf("JS LOAD FAIL: %s\n", msg ? msg : "(no message)");
        if (msg != 0) JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, exc);
        JS_FreeValue(ctx, v);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        free(buf);
        return 1;
    }
    JS_FreeValue(ctx, v);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    free(buf);
    printf("JS MODULE OK\n");
    return 0;
}
