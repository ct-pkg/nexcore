#ifndef IFDEF_SHIM_H
#define IFDEF_SHIM_H

#define NEXCORE_FEATURE 1

static inline int shim_feature_value(void) {
#ifdef NEXCORE_FEATURE
    return 1;
#else
    return 0;
#endif
}

#endif
