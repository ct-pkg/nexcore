/* 与 experiments/src/layout.ct 对照的 C 参照实现。 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

enum NexcoreColor { NEXCORE_RED, NEXCORE_GREEN, NEXCORE_BLUE };

struct NexcoreLayout {
    char c;
    int i;
    long l;
    double d;
    void *p;
    _Bool b;
    enum NexcoreColor e;
};

struct NexcoreAlignProbe {
    char c;
    double d;
};

int main(void) {
    printf("sizeof_char=%zu\n", sizeof(char));
    printf("sizeof_int=%zu\n", sizeof(int));
    printf("sizeof_long=%zu\n", sizeof(long));
    printf("sizeof_size_t=%zu\n", sizeof(size_t));
    printf("sizeof_voidp=%zu\n", sizeof(void *));
    printf("sizeof_double=%zu\n", sizeof(double));
    printf("sizeof_longdouble=%zu\n", sizeof(long double));
    printf("sizeof_bool=%zu\n", sizeof(_Bool));
    printf("sizeof_enum=%zu\n", sizeof(enum NexcoreColor));
    printf("align_double=%zu\n", offsetof(struct NexcoreAlignProbe, d));
    printf("layout_size=%zu\n", sizeof(struct NexcoreLayout));
    printf("layout_off_c=%zu\n", offsetof(struct NexcoreLayout, c));
    printf("layout_off_i=%zu\n", offsetof(struct NexcoreLayout, i));
    printf("layout_off_l=%zu\n", offsetof(struct NexcoreLayout, l));
    printf("layout_off_d=%zu\n", offsetof(struct NexcoreLayout, d));
    printf("layout_off_p=%zu\n", offsetof(struct NexcoreLayout, p));
    printf("layout_off_b=%zu\n", offsetof(struct NexcoreLayout, b));
    printf("layout_off_e=%zu\n", offsetof(struct NexcoreLayout, e));
    return 0;
}
