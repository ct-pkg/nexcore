/* GENERATED FROM tests/fixture.schema.yaml -- DO NOT EDIT. */
/* generator: nexcore-gen-schema 0.1.0  schema_hash: f7554c34c7bf46f8a896a76a779a96c5b1eedc29ce93539de2bebd6b7c4a45b2 */

#include "nexcore_registry.h"

static const struct nx_gen_method nx_gen_method_table[] = {
    {"echo", NX_GEN_DIR_UI2JS, "string", 1},
    {"ping", NX_GEN_DIR_JS2UI, "string", 0}
};

const struct nx_gen_method *const nx_gen_methods = nx_gen_method_table;
const size_t nx_gen_method_count =
    sizeof(nx_gen_method_table) / sizeof(nx_gen_method_table[0]);

/* fixture::echo —— 骨架阶段的 wrapper：业务 handler 未接，统一返回 NX_E_INTERNAL。 */
int32_t nx_gen_fixture_echo(const char *text, char **out, size_t *out_len)
{
    (void)text;
    (void)out;
    (void)out_len;
    return NX_E_INTERNAL;
}

/* fixture::ping —— 骨架阶段的 wrapper：业务 handler 未接，统一返回 NX_E_INTERNAL。 */
int32_t nx_gen_fixture_ping(char **out, size_t *out_len)
{
    (void)out;
    (void)out_len;
    return NX_E_INTERNAL;
}
