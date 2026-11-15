/* ⑬ ACC-A-11 Schema 不匹配：
 *   1. 错误 schema_hash -> NX_E_SCHEMA_MISMATCH，且 *out == NULL
 *   2. 同进程立刻用正确 hash 重试 -> NX_OK（证明失败路径已回收 JS runtime/队列/impl）
 *   3. schema_hash == NULL（debug 未提供）-> NX_OK
 *
 * 期望 hash 由 check.sh 用 tools/schema_hash.py --emit-header 现场生成，
 * 与 src/NexCore.ct 的编译期常量同源；测试里不写死。
 */
#include <stdio.h>
#include <string.h>

#include "NexCore.h"
#include "schema_hash_test.h" /* NEXCORE_TEST_SCHEMA_HASH */

static int fail(const char *what, int32_t rc, NexCoreHandle h)
{
    printf("SCHEMA FAIL: %s (rc=%d, handle=%p)\n", what, (int)rc, (void *)h);
    return 1;
}

static void fill_cfg(struct NexCoreConfig *cfg, const char *hash)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->struct_size = (uint32_t)sizeof(*cfg);
    cfg->abi_version = NEXCORE_ABI_VERSION;
    cfg->protocol_version = NEXCORE_PROTOCOL_VERSION;
    cfg->schema_hash = hash;
}

int main(void)
{
    struct NexCoreConfig cfg;
    char wrong[65];
    NexCoreHandle h;
    int32_t rc;

    memcpy(wrong, NEXCORE_TEST_SCHEMA_HASH, sizeof(wrong));
    wrong[0] = (wrong[0] == '0') ? '1' : '0';
    printf("expected hash: %.12s…  wrong hash: %.12s…\n",
           NEXCORE_TEST_SCHEMA_HASH, wrong);

    /* 1. 错误 hash：必须拒绝且不返回句柄 */
    h = (NexCoreHandle)0x1;
    fill_cfg(&cfg, wrong);
    rc = nexcore_create(&cfg, &h);
    printf("create(wrong hash)  rc=%d out=%p\n", (int)rc, (void *)h);
    if (rc != NX_E_SCHEMA_MISMATCH) return fail("wrong hash must be NX_E_SCHEMA_MISMATCH", rc, h);
    if (h != 0) return fail("wrong hash must set *out = NULL", rc, h);

    /* 2. 同进程重试正确 hash：失败路径若漏回收，这里会拿不到句柄 */
    h = 0;
    fill_cfg(&cfg, NEXCORE_TEST_SCHEMA_HASH);
    rc = nexcore_create(&cfg, &h);
    printf("create(retry good)  rc=%d out=%p\n", (int)rc, (void *)h);
    if (rc != NX_OK || h == 0) return fail("retry with good hash must succeed", rc, h);
    if (nexcore_shutdown(h) != NX_OK) return fail("shutdown", NX_OK, h);
    if (nexcore_free(h) != NX_OK) return fail("free", NX_OK, h);

    /* 3. NULL：design §6.2 允许 debug 不提供 */
    h = 0;
    fill_cfg(&cfg, 0);
    rc = nexcore_create(&cfg, &h);
    printf("create(NULL hash)   rc=%d out=%p\n", (int)rc, (void *)h);
    if (rc != NX_OK || h == 0) return fail("NULL hash must be accepted", rc, h);
    if (nexcore_shutdown(h) != NX_OK) return fail("shutdown", NX_OK, h);
    if (nexcore_free(h) != NX_OK) return fail("free", NX_OK, h);

    printf("SCHEMA MISMATCH OK\n");
    return 0;
}
