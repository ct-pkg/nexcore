/* ⑫ ACC-A-04 ABI 版本/布局冻结的运行时负例：
 *   - struct_size < MIN            -> NX_E_INVALID_ARGUMENT，*out 保持 NULL
 *   - reserved != 0                -> NX_E_INVALID_ARGUMENT
 *   - abi_version 不匹配           -> NX_E_INVALID_ARGUMENT
 *   - struct_size == MIN（旧调用方）-> NX_OK（缺失尾字段按默认值）
 *   - struct_size > CURRENT + 尾部垃圾（新调用方）-> NX_OK，尾部必须被忽略
 *
 * 布局 golden 本身的比对在 check.sh ⑪/⑫（tools/abi_layout.py），这里只验运行时协商。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "NexCore.h"

/* 模拟“比当前头文件更新的调用方”：前缀必须仍是 struct NexCoreConfig 的布局。 */
struct BigConfig {
    struct NexCoreConfig base;
    unsigned char tail[64];
};

static int fail(const char *what, int32_t rc, NexCoreHandle h)
{
    printf("CONFIG FAIL: %s (rc=%d, handle=%p)\n", what, (int)rc, (void *)h);
    return 1;
}

static void fill_base(struct NexCoreConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->struct_size = (uint32_t)sizeof(struct NexCoreConfig);
    cfg->abi_version = NEXCORE_ABI_VERSION;
    cfg->protocol_version = NEXCORE_PROTOCOL_VERSION;
    cfg->default_timeout_ms = NEXCORE_DEFAULT_TIMEOUT_MS;
    cfg->max_message_bytes = NEXCORE_DEFAULT_MAX_MESSAGE_BYTES;
    cfg->max_pending = NEXCORE_DEFAULT_MAX_PENDING;
}

static int expect_reject(const char *what, struct NexCoreConfig *cfg)
{
    NexCoreHandle h = (NexCoreHandle)0x1; /* 预置垃圾，验证失败路径会清成 NULL */
    int32_t rc = nexcore_create(cfg, &h);

    printf("%-28s rc=%d out=%p\n", what, (int)rc, (void *)h);
    if (rc != NX_E_INVALID_ARGUMENT) return fail(what, rc, h);
    if (h != 0) return fail("失败路径必须把 *out 写成 NULL", rc, h);
    return 0;
}

static int expect_ok(const char *what, const struct NexCoreConfig *cfg)
{
    NexCoreHandle h = 0;
    int32_t rc = nexcore_create(cfg, &h);

    printf("%-28s rc=%d out=%p\n", what, (int)rc, (void *)h);
    if (rc != NX_OK || h == 0) return fail(what, rc, h);
    if (nexcore_shutdown(h) != NX_OK) return fail("shutdown", rc, h);
    if (nexcore_free(h) != NX_OK) return fail("free", rc, h);
    return 0;
}

int main(void)
{
    struct NexCoreConfig cfg;
    struct BigConfig *big;
    size_t i;

    /* 负例 1：struct_size 比 MIN 少 1（旧调用方传了坏长度） */
    fill_base(&cfg);
    cfg.struct_size = (uint32_t)(NEXCORE_CONFIG_MIN_SIZE - 1);
    if (expect_reject("struct_size = MIN-1", &cfg) != 0) return 1;

    /* 负例 2：reserved 非 0（未来复用字段必须先升 abi_version） */
    fill_base(&cfg);
    cfg.reserved = 1;
    if (expect_reject("reserved != 0", &cfg) != 0) return 1;

    /* 负例 3：abi_version 不匹配（改布局却没升版本 / 调用方版本过新） */
    fill_base(&cfg);
    cfg.abi_version = NEXCORE_ABI_VERSION + 1;
    if (expect_reject("abi_version + 1", &cfg) != 0) return 1;

    /* 正例 1：严格等于 MIN，尾部字段视为未提供 */
    {
        unsigned char *raw = (unsigned char *)calloc(1, sizeof(struct NexCoreConfig));
        struct NexCoreConfig *old;

        if (raw == 0) return fail("calloc", NX_E_INTERNAL, 0);
        old = (struct NexCoreConfig *)raw;
        fill_base(old);
        old->struct_size = (uint32_t)NEXCORE_CONFIG_MIN_SIZE;
        if (expect_ok("struct_size = MIN", old) != 0) {
            free(raw);
            return 1;
        }
        free(raw);
    }

    /* 正例 2：struct_size > CURRENT 且尾部全是垃圾，库必须忽略尾部 */
    big = (struct BigConfig *)malloc(sizeof(struct BigConfig));
    if (big == 0) return fail("malloc", NX_E_INTERNAL, 0);
    fill_base(&big->base);
    for (i = 0; i < sizeof(big->tail); i++) big->tail[i] = 0xA5;
    big->base.struct_size = (uint32_t)sizeof(struct BigConfig);
    if (expect_ok("struct_size = CURRENT+64", &big->base) != 0) {
        free(big);
        return 1;
    }
    free(big);

    /* 正例 3：当前大小，正常路径 */
    fill_base(&cfg);
    if (expect_ok("struct_size = CURRENT", &cfg) != 0) return 1;

    printf("CONFIG NEGOTIATION OK\n");
    return 0;
}
