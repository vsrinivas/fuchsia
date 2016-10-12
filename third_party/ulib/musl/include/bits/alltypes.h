#if defined(__x86_64__)
#include "x86_64/alltypes.h"
#elif defined(__aarch64__)
#include "aarch64/alltypes.h"
#elif defined(__arm__)
#include "arm/alltypes.h"
#else
#error Unsupported architecture!
#endif

#if defined(__arm__) || defined(__i386__)
// The arm-eabi and i386-elf GCC target uses 'long' types for these, which is
// inconsistent with everything else.  Both the arm-linux targets,
// and the *-elf targets, use 'int' for 32-bit types.  Since it's so
// near-universal, it makes life simpler to rely on e.g. using plain
// %x et al in printf formats for uint32_t and friends.
#undef __CHAR32_TYPE__
#undef __INT32_TYPE__
#undef __UINT32_TYPE__
#undef __INT_LEAST32_TYPE__
#undef __UINT_LEAST32_TYPE__
#undef __INT32_MAX__
#undef __UINT32_MAX__
#undef __INT_LEAST32_MAX__
#undef __UINT_LEAST32_MAX__
#undef __INT32_C
#undef __UINT32_C
#define __CHAR32_TYPE__       unsigned int
#define __INT32_TYPE__        int
#define __UINT32_TYPE__       unsigned int
#define __INT_LEAST32_TYPE__  int
#define __UINT_LEAST32_TYPE__ unsigned int
#define __INT32_C(c)          c
#define __UINT32_C(c)         c##U
#define __INT32_MAX__         0x7fffffff
#define __UINT32_MAX__        0xffffffffU
#define __INT_LEAST32_MAX__   __INT32_MAX__
#define __UINT_LEAST32_MAX__  __UINT32_MAX__
#endif

#if defined(__NEED_uint8_t) && !defined(__DEFINED_uint8_t)
typedef __UINT8_TYPE__     uint8_t;
#define __DEFINED_uint8_t
#endif

#if defined(__NEED_uint16_t) && !defined(__DEFINED_uint16_t)
typedef __UINT16_TYPE__    uint16_t;
#define __DEFINED_uint16_t
#endif

#if defined(__NEED_uint32_t) && !defined(__DEFINED_uint32_t)
typedef __UINT32_TYPE__    uint32_t;
#define __DEFINED_uint32_t
#endif

#if defined(__NEED_uint64_t) && !defined(__DEFINED_uint64_t)
typedef __UINT64_TYPE__    uint64_t;
#define __DEFINED_uint64_t
#endif

#if defined(__NEED_int8_t) && !defined(__DEFINED_int8_t)
typedef __INT8_TYPE__      int8_t;
#define __DEFINED_int8_t
#endif

#if defined(__NEED_int16_t) && !defined(__DEFINED_int16_t)
typedef __INT16_TYPE__     int16_t;
#define __DEFINED_int16_t
#endif

#if defined(__NEED_int32_t) && !defined(__DEFINED_int32_t)
typedef __INT32_TYPE__     int32_t;
#define __DEFINED_int32_t
#endif

#if defined(__NEED_int64_t) && !defined(__DEFINED_int64_t)
typedef __INT64_TYPE__     int64_t;
#define __DEFINED_int64_t
#endif

#if defined(__NEED_uint_least8_t) && !defined(__DEFINED_uint_least8_t)
typedef __UINT_LEAST8_TYPE__     uint_least8_t;
#define __DEFINED_uint_least8_t
#endif

#if defined(__NEED_uint_least16_t) && !defined(__DEFINED_uint_least16_t)
typedef __UINT_LEAST16_TYPE__    uint_least16_t;
#define __DEFINED_uint_least16_t
#endif

#if defined(__NEED_uint_least32_t) && !defined(__DEFINED_uint_least32_t)
typedef __UINT_LEAST32_TYPE__    uint_least32_t;
#define __DEFINED_uint_least32_t
#endif

#if defined(__NEED_uint_least64_t) && !defined(__DEFINED_uint_least64_t)
typedef __UINT_LEAST64_TYPE__    uint_least64_t;
#define __DEFINED_uint_least64_t
#endif

#if defined(__NEED_int_least8_t) && !defined(__DEFINED_int_least8_t)
typedef __INT_LEAST8_TYPE__      int_least8_t;
#define __DEFINED_int_least8_t
#endif

#if defined(__NEED_int_least16_t) && !defined(__DEFINED_int_least16_t)
typedef __INT_LEAST16_TYPE__     int_least16_t;
#define __DEFINED_int_least16_t
#endif

#if defined(__NEED_int_least32_t) && !defined(__DEFINED_int_least32_t)
typedef __INT_LEAST32_TYPE__     int_least32_t;
#define __DEFINED_int_least32_t
#endif

#if defined(__NEED_int_least64_t) && !defined(__DEFINED_int_least64_t)
typedef __INT_LEAST64_TYPE__     int_least64_t;
#define __DEFINED_int_least64_t
#endif

#if defined(__NEED_uint_fast8_t) && !defined(__DEFINED_uint_fast8_t)
typedef __UINT_FAST8_TYPE__     uint_fast8_t;
#define __DEFINED_uint_fast8_t
#endif

#if defined(__NEED_uint_fast16_t) && !defined(__DEFINED_uint_fast16_t)
typedef __UINT_FAST16_TYPE__    uint_fast16_t;
#define __DEFINED_uint_fast16_t
#endif

#if defined(__NEED_uint_fast32_t) && !defined(__DEFINED_uint_fast32_t)
typedef __UINT_FAST32_TYPE__    uint_fast32_t;
#define __DEFINED_uint_fast32_t
#endif

#if defined(__NEED_uint_fast64_t) && !defined(__DEFINED_uint_fast64_t)
typedef __UINT_FAST64_TYPE__    uint_fast64_t;
#define __DEFINED_uint_fast64_t
#endif

#if defined(__NEED_int_fast8_t) && !defined(__DEFINED_int_fast8_t)
typedef __INT_FAST8_TYPE__      int_fast8_t;
#define __DEFINED_int_fast8_t
#endif

#if defined(__NEED_int_fast16_t) && !defined(__DEFINED_int_fast16_t)
typedef __INT_FAST16_TYPE__     int_fast16_t;
#define __DEFINED_int_fast16_t
#endif

#if defined(__NEED_int_fast32_t) && !defined(__DEFINED_int_fast32_t)
typedef __INT_FAST32_TYPE__     int_fast32_t;
#define __DEFINED_int_fast32_t
#endif

#if defined(__NEED_int_fast64_t) && !defined(__DEFINED_int_fast64_t)
typedef __INT_FAST64_TYPE__     int_fast64_t;
#define __DEFINED_int_fast64_t
#endif

#if defined(__NEED_intptr_t) && !defined(__DEFINED_intptr_t)
typedef __INTPTR_TYPE__ intptr_t;
#define __DEFINED_intptr_t
#endif

#if defined(__NEED_uintptr_t) && !defined(__DEFINED_uintptr_t)
typedef __UINTPTR_TYPE__ uintptr_t;
#define __DEFINED_uintptr_t
#endif

#if defined(__NEED_intmax_t) && !defined(__DEFINED_intmax_t)
typedef __INTMAX_TYPE__ intmax_t;
#define __DEFINED_intmax_t
#endif

#if defined(__NEED_uintmax_t) && !defined(__DEFINED_uintmax_t)
typedef __UINTMAX_TYPE__ uintmax_t;
#define __DEFINED_uintmax_t
#endif

#ifndef __cplusplus
#if defined(__NEED_wchar_t) && !defined(__DEFINED_wchar_t)
typedef __WCHAR_TYPE__ wchar_t;
#define __DEFINED_wchar_t
#endif
#endif

#if defined(__NEED_wint_t) && !defined(__DEFINED_wint_t)
typedef unsigned wint_t;
#define __DEFINED_wint_t
#endif

#if defined(__NEED_size_t) && !defined(__DEFINED_size_t)
typedef __SIZE_TYPE__ size_t;
#define __DEFINED_size_t
#endif

#if defined(__NEED_ptrdiff_t) && !defined(__DEFINED_ptrdiff_t)
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#define __DEFINED_ptrdiff_t
#endif

#if defined(__NEED_va_list) && !defined(__DEFINED_va_list)
typedef __builtin_va_list va_list;
#define __DEFINED_va_list
#endif

#if defined(__NEED___isoc_va_list) && !defined(__DEFINED___isoc_va_list)
typedef __builtin_va_list __isoc_va_list;
#define __DEFINED___isoc_va_list
#endif

#if defined(__NEED_time_t) && !defined(__DEFINED_time_t)
typedef long time_t;
#define __DEFINED_time_t
#endif

#if defined(__NEED_max_align_t) && !defined(__DEFINED_max_align_t)
typedef struct {
    long long __ll;
    long double __ld;
} max_align_t;
#define __DEFINED_max_align_t
#endif

#if defined(__NEED_suseconds_t) && !defined(__DEFINED_suseconds_t)
typedef long suseconds_t;
#define __DEFINED_suseconds_t
#endif

#if defined(__NEED_clock_t) && !defined(__DEFINED_clock_t)
typedef long clock_t;
#define __DEFINED_clock_t
#endif

#if defined(__NEED_pid_t) && !defined(__DEFINED_pid_t)
typedef int pid_t;
#define __DEFINED_pid_t
#endif

#if defined(__NEED_id_t) && !defined(__DEFINED_id_t)
typedef unsigned id_t;
#define __DEFINED_id_t
#endif

#if defined(__NEED_uid_t) && !defined(__DEFINED_uid_t)
typedef unsigned uid_t;
#define __DEFINED_uid_t
#endif

#if defined(__NEED_gid_t) && !defined(__DEFINED_gid_t)
typedef unsigned gid_t;
#define __DEFINED_gid_t
#endif

#if defined(__NEED_struct_iovec) && !defined(__DEFINED_struct_iovec)
struct iovec {
    void* iov_base;
    size_t iov_len;
};
#define __DEFINED_struct_iovec
#endif

#if defined(__NEED_struct_timeval) && !defined(__DEFINED_struct_timeval)
struct timeval {
    time_t tv_sec;
    suseconds_t tv_usec;
};
#define __DEFINED_struct_timeval
#endif

#if defined(__NEED_struct_timespec) && !defined(__DEFINED_struct_timespec)
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};
#define __DEFINED_struct_timespec
#endif
