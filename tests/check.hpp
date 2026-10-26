#pragma once

// Minimal always-on check macro. Unlike <cassert>, OB_CHECK stays active in
// Release builds (where NDEBUG would silently disable assert), so CI runs with
// CMAKE_BUILD_TYPE=Release still enforce every check.
#include <cstdio>
#include <cstdlib>

#define OB_CHECK(cond)                                                       \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "OB_CHECK failed: %s\n  at %s:%d\n",        \
                         #cond, __FILE__, __LINE__);                         \
            std::abort();                                                    \
        }                                                                    \
    } while (0)
