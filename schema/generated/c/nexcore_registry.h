/* GENERATED FROM tests/fixture.schema.yaml -- DO NOT EDIT. */
/* generator: nexcore-gen-schema 0.1.0  schema_hash: f7554c34c7bf46f8a896a76a779a96c5b1eedc29ce93539de2bebd6b7c4a45b2 */

#ifndef NEXCORE_GENERATED_FIXTURE_REGISTRY_H
#define NEXCORE_GENERATED_FIXTURE_REGISTRY_H

#include <stddef.h>
#include <stdint.h>
#include "NexCore.h"

typedef enum {
    NX_GEN_DIR_UI2JS = 0,
    NX_GEN_DIR_JS2UI = 1
} nx_gen_dir;

struct nx_gen_method {
    const char *name;
    nx_gen_dir direction;
    const char *result_type;
    size_t param_count;
};

int32_t nx_gen_fixture_echo(const char *text, char **out, size_t *out_len);
int32_t nx_gen_fixture_ping(char **out, size_t *out_len);

extern const struct nx_gen_method *const nx_gen_methods;
extern const size_t nx_gen_method_count;

#endif /* NEXCORE_GENERATED_FIXTURE_REGISTRY_H */
