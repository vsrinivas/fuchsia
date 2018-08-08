// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_MACROS_H_
#define GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_MACROS_H_

#include <assert.h>
#include <limits.h> // PAGE_SIZE
#include <stdarg.h>
#include <stdio.h>

// Files #including macros.h may assume that it #includes inttypes.h.
// So, for convenience, they don't need to follow "#include-what-you-use" for that header.
#include <inttypes.h>

#ifndef MAGMA_DEBUG_INTERNAL_USE_ONLY
#error MAGMA_DEBUG_INTERNAL_USE_ONLY not defined, your gn foo needs magma_util_config
#endif

namespace magma {

static constexpr bool kDebug = MAGMA_DEBUG_INTERNAL_USE_ONLY;

#define DASSERT(...)                                                                               \
    do {                                                                                           \
        if (magma::kDebug && !(__VA_ARGS__)) {                                                     \
            printf("%s:%d ASSERT\n", __FILE__, __LINE__);                                          \
            assert(__VA_ARGS__);                                                                   \
        }                                                                                          \
    } while (0)

static constexpr bool kMagmaDretEnable = kDebug;

template <typename T>
__attribute__((format(printf, 4, 5))) static inline T dret(const char* file, int line, T ret,
                                                           const char* msg, ...)
{
    printf("%s:%d returning error %d", file, line, ret);
    if (msg) {
        va_list args;
        va_start(args, msg);
        printf(": ");
        vprintf(msg, args);
        va_end(args);
    }
    printf("\n");
    return ret;
}

#define DRET(ret)                                                                                  \
    (magma::kMagmaDretEnable ? (ret == 0 ? ret : magma::dret(__FILE__, __LINE__, ret, nullptr))    \
                             : ret)

// Must provide const char* msg as the 2nd paramter; other parameters optional.
#define DRET_MSG(ret, ...)                                                                         \
    (magma::kMagmaDretEnable                                                                       \
         ? (ret == 0 ? ret : magma::dret(__FILE__, __LINE__, ret, __VA_ARGS__))                    \
         : ret)

__attribute__((format(printf, 3, 4))) static inline bool dret_false(const char* file, int line,
                                                                    const char* msg, ...)
{
    printf("%s:%d returning false: ", file, line);
    va_list args;
    va_start(args, msg);
    vprintf(msg, args);
    va_end(args);
    printf("\n");
    return false;
}

// Must provide const char* msg as the 2nd paramter; other parameters optional.
#define DRETF(ret, ...)                                                                            \
    (magma::kMagmaDretEnable                                                                       \
         ? (ret == true ? true : magma::dret_false(__FILE__, __LINE__, __VA_ARGS__))               \
         : ret)

__attribute__((format(printf, 3, 4))) static inline void dret_null(const char* file, int line,
                                                                   const char* msg, ...)
{
    printf("%s:%d returning null: ", file, line);
    va_list args;
    va_start(args, msg);
    vprintf(msg, args);
    va_end(args);
    printf("\n");
}

// Must provide const char* msg as the 2nd paramter; other parameters optional.
#define DRETP(ret, ...)                                                                            \
    (magma::kMagmaDretEnable                                                                       \
         ? (ret != nullptr ? ret : (magma::dret_null(__FILE__, __LINE__, __VA_ARGS__), nullptr))   \
         : ret)

enum LogLevel { LOG_WARNING, LOG_INFO };

__attribute__((format(printf, 2, 3))) static inline void log(LogLevel level, const char* msg, ...)
{
    switch (level) {
        case LOG_WARNING:
            printf("[WARNING] ");
            break;
        case LOG_INFO:
            printf("[INFO] ");
            break;
    }
    va_list args;
    va_start(args, msg);
    vprintf(msg, args);
    va_end(args);
    printf("\n");
}

#define UNIMPLEMENTED(...)                                                                         \
    do {                                                                                           \
        DLOG("UNIMPLEMENTED: " #__VA_ARGS__);                                                      \
        DASSERT(false);                                                                            \
    } while (0)

#define ATTRIBUTE_UNUSED __attribute__((unused))

#ifndef DISALLOW_COPY_AND_ASSIGN
#define DISALLOW_COPY_AND_ASSIGN(TypeName)                                                         \
    TypeName(const TypeName&) = delete;                                                            \
    void operator=(const TypeName&) = delete
#endif

static inline bool is_page_aligned(uint64_t val) { return (val & (PAGE_SIZE - 1)) == 0; }

static inline uint32_t upper_32_bits(uint64_t n) { return static_cast<uint32_t>(n >> 32); }

static inline uint32_t lower_32_bits(uint64_t n) { return static_cast<uint32_t>(n); }

static inline bool get_pow2(uint64_t val, uint64_t* pow2_out)
{
    if (val == 0)
        return DRETF(false, "zero is not a power of two");

    uint64_t result = 0;
    while ((val & 1) == 0) {
        val >>= 1;
        result++;
    }
    if (val >> 1)
        return DRETF(false, "not a power of 2");

    *pow2_out = result;
    return true;
}

static inline bool is_pow2(uint64_t val)
{
    uint64_t out;
    return get_pow2(val, &out);
}

// Note, alignment must be a power of 2
template <class T> static inline T round_up(T val, uint32_t alignment)
{
    DASSERT(is_pow2(alignment));
    return ((val - 1) | (alignment - 1)) + 1;
}

static inline uint64_t ns_to_ms(uint64_t ns) { return ns / 1000000ull; }

} // namespace magma

#endif // GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_MACROS_H_
