#include <stdio.h>
#include "trait_abi.h"

int main(void) {
    printf("ct plain: size=%d a=%d b=%d\n",
           tl_plain_size(), tl_plain_off_a(), tl_plain_off_b());
    printf("ct one:   size=%d a=%d b=%d vptr=%d\n",
           tl_one_size(), tl_one_off_a(), tl_one_off_b(), tl_one_off_vptr());
    printf("ct two:   size=%d a=%d b=%d ping=%d pong=%d\n",
           tl_two_size(), tl_two_off_a(), tl_two_off_b(),
           tl_two_off_ping(), tl_two_off_pong());
    printf("ct holder:     size=%d tag=%d inner=%d tail=%d\n",
           tl_holder_size(), tl_holder_off_tag(),
           tl_holder_off_inner(), tl_holder_off_tail());
    printf("ct ptrholder:  size=%d tag=%d inner=%d tail=%d\n",
           tl_ptrholder_size(), tl_ptrholder_off_tag(),
           tl_ptrholder_off_inner(), tl_ptrholder_off_tail());
    return 0;
}
