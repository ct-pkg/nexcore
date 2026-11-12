#ifndef ERRNO_SHIM_H
#define ERRNO_SHIM_H

#include <errno.h>

static inline int shim_get_errno(void) { return errno; }
static inline void shim_set_errno(int v) { errno = v; }

#endif
