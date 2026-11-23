#pragma once

#ifdef _MSC_VER
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#if !defined(__clang__)
#include <cmath>
#include <cstdint>
#include <immintrin.h>
#include <intrin.h>
#include <limits>

static inline int __builtin_ctzll (unsigned long long x)
{
    unsigned long idx = 0;
    return _BitScanForward64 (&idx, x) ? int (idx) : 64;
}

static inline int __builtin_ctz (unsigned int x)
{
    unsigned long idx = 0;
    return _BitScanForward (&idx, x) ? int (idx) : 32;
}

static inline unsigned int __builtin_ia32_stmxcsr()
{
    return _mm_getcsr();
}

static inline void __builtin_ia32_ldmxcsr (unsigned int x)
{
    _mm_setcsr (x);
}
#endif
#endif
