/* GENERATED FROM tests/fixture.schema.yaml -- DO NOT EDIT. */
/* generator: nexcore-gen-schema 0.1.0  schema_hash: f7554c34c7bf46f8a896a76a779a96c5b1eedc29ce93539de2bebd6b7c4a45b2 */

#include "nexcore_registry.h"

#include <stdlib.h>
#include <string.h>

static const struct nx_gen_provider *nx_gen_active_provider;

int32_t nx_gen_register_provider(const struct nx_gen_provider *provider)
{
    nx_gen_active_provider = provider;
    return NX_OK;
}

/* 调用点 1：fixture::echo —— typed wrapper 直接派发到 provider。 */
int32_t nx_gen_fixture_echo(const char * text, char **out, size_t *out_len)
{
    if (nx_gen_active_provider == 0 || nx_gen_active_provider->echo == 0) {
        return NX_E_INTERNAL;
    }
    return nx_gen_active_provider->echo(text, out, out_len);
}

/* 调用点 1：fixture::ping —— typed wrapper 直接派发到 provider。 */
int32_t nx_gen_fixture_ping(char **out, size_t *out_len)
{
    if (nx_gen_active_provider == 0 || nx_gen_active_provider->ping == 0) {
        return NX_E_INTERNAL;
    }
    return nx_gen_active_provider->ping(out, out_len);
}

/* 调用点 2：fixture::echo —— registry adaptor（argc + 类型转换）。 */
static int32_t nx_gen_fixture_echo_invoke(const char *const *argv, size_t argc,
                                          char **out, size_t *out_len)
{
    const char * text;

    if (argc != 1) return NX_E_INVALID_ARGUMENT;
    if (argv == 0) return NX_E_INVALID_ARGUMENT;
    text = argv[0];
    return nx_gen_fixture_echo(text, out, out_len);
}

/* 调用点 2：fixture::ping —— registry adaptor（argc + 类型转换）。 */
static int32_t nx_gen_fixture_ping_invoke(const char *const *argv, size_t argc,
                                          char **out, size_t *out_len)
{
    (void)argv;
    if (argc != 0) return NX_E_INVALID_ARGUMENT;
    return nx_gen_fixture_ping(out, out_len);
}

static const struct nx_gen_method nx_gen_method_table[] = {
    {"echo", NX_GEN_DIR_UI2JS, "string", 1, nx_gen_fixture_echo_invoke},
    {"ping", NX_GEN_DIR_JS2UI, "string", 0, nx_gen_fixture_ping_invoke}
};

const struct nx_gen_method *const nx_gen_methods = nx_gen_method_table;
const size_t nx_gen_method_count =
    sizeof(nx_gen_method_table) / sizeof(nx_gen_method_table[0]);

int32_t nx_gen_dispatch(const char *name, const char *const *argv, size_t argc,
                        char **out, size_t *out_len)
{
    size_t i;

    if (out != 0) *out = 0;
    if (out_len != 0) *out_len = 0;
    if (name == 0) return NX_E_INVALID_ARGUMENT;
    for (i = 0; i < nx_gen_method_count; i = i + 1) {
        if (strcmp(nx_gen_method_table[i].name, name) == 0) {
            return nx_gen_method_table[i].invoke(argv, argc, out, out_len);
        }
    }
    return NX_E_INVALID_ARGUMENT;
}
