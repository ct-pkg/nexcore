#include <stdio.h>
#include <stdint.h>
#include "abi.h"

static int host_cb(void *ctx, int v) {
    int *base = (int *)ctx;
    return *base + v;
}

int main(void) {
    int base = 40;
    struct NexcorePair p;

    printf("host add=%d version=%d\n", nexcore_abi_add(20, 22), nexcore_abi_version());
    printf("host pair sizeof=%d right_off=%d ctx_off=%d\n",
           nexcore_pair_size(),
           nexcore_pair_right_offset(),
           nexcore_pair_ctx_offset());
    p = nexcore_pair_make(7, 2.5, (void *)&base);
    printf("host make left=%d right=%.1f ctx_ok=%d\n",
           p.left, p.right, p.ctx == (void *)&base);
    printf("host apply=%d\n", nexcore_apply((void *)&base, host_cb, 2));
    return 0;
}
