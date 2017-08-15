// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

// The compiler predefines macros __<type>_TYPE__, __<type>_MAX__,
// and __<type>_C for the various types.  All we have to do here is
// define the public names based on those macros.

// Clang predefines macros __<type>_FMT<letter>__ for each type,
// with <letter> being i and for signed types, and o, u, x, and X
// for unsigned types.  That lets <inttypes.h> do its work without
// any special knowledge of what the underlying types are.
// Unfortunately, GCC does not define these macros.  To keep all
// knowledge of the compiler's choices isolated to this file, define
// them here for GCC so that <inttypes.h> can use them unconditionally.
#ifndef __INTMAX_FMTd__

#define __INT8_FMT_MODIFIER__        "hh"
#define __INT16_FMT_MODIFIER__       "h"
#define __INT32_FMT_MODIFIER__       ""

#define __INT_LEAST8_FMT_MODIFIER__  __INT8_FMT_MODIFIER__
#define __INT_LEAST16_FMT_MODIFIER__ __INT16_FMT_MODIFIER__
#define __INT_LEAST32_FMT_MODIFIER__ __INT32_FMT_MODIFIER__
#define __INT_LEAST64_FMT_MODIFIER__ __INT64_FMT_MODIFIER__

// The *-elf and arm-eabi GCC targets use 'int' for the fast{8,16,32}
// types. On LP64 systems, 'long' is used for the fast64 type, and
// 'long long' on non-LP64 systems.
#define __INT_FAST8_FMT_MODIFIER__   ""
#define __INT_FAST16_FMT_MODIFIER__  ""
#define __INT_FAST32_FMT_MODIFIER__  ""
#if _LP64
#define __INT_FAST64_FMT_MODIFIER__  "l"
#else
#define __INT_FAST64_FMT_MODIFIER__  "ll"
#endif

// On machines where 'long' types are 64 bits, the compiler defines
// __INT64_TYPE__ et al using 'long', not 'long long', though both are
// 64-bit types.
#ifdef _LP64
#define __INT64_FMT_MODIFIER__       "l"
#define __INTPTR_FMT_MODIFIER__      "l"
#else
#define __INT64_FMT_MODIFIER__       "ll"
#define __INTPTR_FMT_MODIFIER__      ""
#endif

#define __INTMAX_FMT_MODIFIER__      __INT64_FMT_MODIFIER__

#define __INTMAX_FMTd__              __INTMAX_FMT_MODIFIER__ "d"
#define __INTMAX_FMTi__              __INTMAX_FMT_MODIFIER__ "i"
#define __UINTMAX_FMTo__             __INTMAX_FMT_MODIFIER__ "o"
#define __UINTMAX_FMTu__             __INTMAX_FMT_MODIFIER__ "u"
#define __UINTMAX_FMTx__             __INTMAX_FMT_MODIFIER__ "x"
#define __UINTMAX_FMTX__             __INTMAX_FMT_MODIFIER__ "X"
#define __INTPTR_FMTd__              __INTPTR_FMT_MODIFIER__ "d"
#define __INTPTR_FMTi__              __INTPTR_FMT_MODIFIER__ "i"
#define __UINTPTR_FMTo__             __INTPTR_FMT_MODIFIER__ "o"
#define __UINTPTR_FMTu__             __INTPTR_FMT_MODIFIER__ "u"
#define __UINTPTR_FMTx__             __INTPTR_FMT_MODIFIER__ "x"
#define __UINTPTR_FMTX__             __INTPTR_FMT_MODIFIER__ "X"
#define __INT8_FMTd__                __INT8_FMT_MODIFIER__ "d"
#define __INT8_FMTi__                __INT8_FMT_MODIFIER__ "i"
#define __INT16_FMTd__               __INT16_FMT_MODIFIER__ "d"
#define __INT16_FMTi__               __INT16_FMT_MODIFIER__ "i"
#define __INT32_FMTd__               __INT32_FMT_MODIFIER__ "d"
#define __INT32_FMTi__               __INT32_FMT_MODIFIER__ "i"
#define __INT64_FMTd__               __INT64_FMT_MODIFIER__ "d"
#define __INT64_FMTi__               __INT64_FMT_MODIFIER__ "i"
#define __UINT8_FMTo__               __INT8_FMT_MODIFIER__ "o"
#define __UINT8_FMTu__               __INT8_FMT_MODIFIER__ "u"
#define __UINT8_FMTx__               __INT8_FMT_MODIFIER__ "x"
#define __UINT8_FMTX__               __INT8_FMT_MODIFIER__ "X"
#define __UINT16_FMTo__              __INT16_FMT_MODIFIER__ "o"
#define __UINT16_FMTu__              __INT16_FMT_MODIFIER__ "u"
#define __UINT16_FMTx__              __INT16_FMT_MODIFIER__ "x"
#define __UINT16_FMTX__              __INT16_FMT_MODIFIER__ "X"
#define __UINT32_FMTo__              __INT32_FMT_MODIFIER__ "o"
#define __UINT32_FMTu__              __INT32_FMT_MODIFIER__ "u"
#define __UINT32_FMTx__              __INT32_FMT_MODIFIER__ "x"
#define __UINT32_FMTX__              __INT32_FMT_MODIFIER__ "X"
#define __UINT64_FMTo__              __INT64_FMT_MODIFIER__ "o"
#define __UINT64_FMTu__              __INT64_FMT_MODIFIER__ "u"
#define __UINT64_FMTx__              __INT64_FMT_MODIFIER__ "x"
#define __UINT64_FMTX__              __INT64_FMT_MODIFIER__ "X"
#define __INT_LEAST8_FMTd__          __INT_LEAST8_FMT_MODIFIER__ "d"
#define __INT_LEAST8_FMTi__          __INT_LEAST8_FMT_MODIFIER__ "i"
#define __UINT_LEAST8_FMTo__         __INT_LEAST8_FMT_MODIFIER__ "o"
#define __UINT_LEAST8_FMTu__         __INT_LEAST8_FMT_MODIFIER__ "u"
#define __UINT_LEAST8_FMTx__         __INT_LEAST8_FMT_MODIFIER__ "x"
#define __UINT_LEAST8_FMTX__         __INT_LEAST8_FMT_MODIFIER__ "X"
#define __INT_LEAST16_FMTd__         __INT_LEAST16_FMT_MODIFIER__ "d"
#define __INT_LEAST16_FMTi__         __INT_LEAST16_FMT_MODIFIER__ "i"
#define __UINT_LEAST16_FMTo__        __INT_LEAST16_FMT_MODIFIER__ "o"
#define __UINT_LEAST16_FMTu__        __INT_LEAST16_FMT_MODIFIER__ "u"
#define __UINT_LEAST16_FMTx__        __INT_LEAST16_FMT_MODIFIER__ "x"
#define __UINT_LEAST16_FMTX__        __INT_LEAST16_FMT_MODIFIER__ "X"
#define __INT_LEAST32_FMTd__         __INT_LEAST32_FMT_MODIFIER__ "d"
#define __INT_LEAST32_FMTi__         __INT_LEAST32_FMT_MODIFIER__ "i"
#define __UINT_LEAST32_FMTo__        __INT_LEAST32_FMT_MODIFIER__ "o"
#define __UINT_LEAST32_FMTu__        __INT_LEAST32_FMT_MODIFIER__ "u"
#define __UINT_LEAST32_FMTx__        __INT_LEAST32_FMT_MODIFIER__ "x"
#define __UINT_LEAST32_FMTX__        __INT_LEAST32_FMT_MODIFIER__ "X"
#define __INT_LEAST64_FMTd__         __INT_LEAST64_FMT_MODIFIER__ "d"
#define __INT_LEAST64_FMTi__         __INT_LEAST64_FMT_MODIFIER__ "i"
#define __UINT_LEAST64_FMTo__        __INT_LEAST64_FMT_MODIFIER__ "o"
#define __UINT_LEAST64_FMTu__        __INT_LEAST64_FMT_MODIFIER__ "u"
#define __UINT_LEAST64_FMTx__        __INT_LEAST64_FMT_MODIFIER__ "x"
#define __UINT_LEAST64_FMTX__        __INT_LEAST64_FMT_MODIFIER__ "X"
#define __INT_FAST8_FMTd__           __INT_FAST8_FMT_MODIFIER__ "d"
#define __INT_FAST8_FMTi__           __INT_FAST8_FMT_MODIFIER__ "i"
#define __UINT_FAST8_FMTo__          __INT_FAST8_FMT_MODIFIER__ "o"
#define __UINT_FAST8_FMTu__          __INT_FAST8_FMT_MODIFIER__ "u"
#define __UINT_FAST8_FMTx__          __INT_FAST8_FMT_MODIFIER__ "x"
#define __UINT_FAST8_FMTX__          __INT_FAST8_FMT_MODIFIER__ "X"
#define __INT_FAST16_FMTd__          __INT_FAST16_FMT_MODIFIER__ "d"
#define __INT_FAST16_FMTi__          __INT_FAST16_FMT_MODIFIER__ "i"
#define __UINT_FAST16_FMTo__         __INT_FAST16_FMT_MODIFIER__ "o"
#define __UINT_FAST16_FMTu__         __INT_FAST16_FMT_MODIFIER__ "u"
#define __UINT_FAST16_FMTx__         __INT_FAST16_FMT_MODIFIER__ "x"
#define __UINT_FAST16_FMTX__         __INT_FAST16_FMT_MODIFIER__ "X"
#define __INT_FAST32_FMTd__          __INT_FAST32_FMT_MODIFIER__ "d"
#define __INT_FAST32_FMTi__          __INT_FAST32_FMT_MODIFIER__ "i"
#define __UINT_FAST32_FMTo__         __INT_FAST32_FMT_MODIFIER__ "o"
#define __UINT_FAST32_FMTu__         __INT_FAST32_FMT_MODIFIER__ "u"
#define __UINT_FAST32_FMTx__         __INT_FAST32_FMT_MODIFIER__ "x"
#define __UINT_FAST32_FMTX__         __INT_FAST32_FMT_MODIFIER__ "X"
#define __INT_FAST64_FMTd__          __INT_FAST64_FMT_MODIFIER__ "d"
#define __INT_FAST64_FMTi__          __INT_FAST64_FMT_MODIFIER__ "i"
#define __UINT_FAST64_FMTo__         __INT_FAST64_FMT_MODIFIER__ "o"
#define __UINT_FAST64_FMTu__         __INT_FAST64_FMT_MODIFIER__ "u"
#define __UINT_FAST64_FMTx__         __INT_FAST64_FMT_MODIFIER__ "x"
#define __UINT_FAST64_FMTX__         __INT_FAST64_FMT_MODIFIER__ "X"

#endif

typedef __UINT8_TYPE__     uint8_t;
typedef __UINT16_TYPE__    uint16_t;
typedef __UINT32_TYPE__    uint32_t;
typedef __UINT64_TYPE__    uint64_t;
typedef __INT8_TYPE__      int8_t;
typedef __INT16_TYPE__     int16_t;
typedef __INT32_TYPE__     int32_t;
typedef __INT64_TYPE__     int64_t;

#define UINT8_MAX    __UINT8_MAX__
#define UINT16_MAX   __UINT16_MAX__
#define UINT32_MAX   __UINT32_MAX__
#define UINT64_MAX   __UINT64_MAX__

#define INT8_MAX    __INT8_MAX__
#define INT16_MAX   __INT16_MAX__
#define INT32_MAX   __INT32_MAX__
#define INT64_MAX   __INT64_MAX__

#define INT8_MIN    (-INT8_MAX - 1)
#define INT16_MIN   (-INT16_MAX - 1)
#define INT32_MIN   (-INT32_MAX - 1)
#define INT64_MIN   (-INT64_MAX - 1)

typedef __UINT_LEAST8_TYPE__     uint_least8_t;
typedef __UINT_LEAST16_TYPE__    uint_least16_t;
typedef __UINT_LEAST32_TYPE__    uint_least32_t;
typedef __UINT_LEAST64_TYPE__    uint_least64_t;
typedef __INT_LEAST8_TYPE__      int_least8_t;
typedef __INT_LEAST16_TYPE__     int_least16_t;
typedef __INT_LEAST32_TYPE__     int_least32_t;
typedef __INT_LEAST64_TYPE__     int_least64_t;

#define UINT_LEAST8_MAX    __UINT_LEAST8_MAX__
#define UINT_LEAST16_MAX   __UINT_LEAST16_MAX__
#define UINT_LEAST32_MAX   __UINT_LEAST32_MAX__
#define UINT_LEAST64_MAX   __UINT_LEAST64_MAX__

#define INT_LEAST8_MAX    __INT_LEAST8_MAX__
#define INT_LEAST16_MAX   __INT_LEAST16_MAX__
#define INT_LEAST32_MAX   __INT_LEAST32_MAX__
#define INT_LEAST64_MAX   __INT_LEAST64_MAX__

#define INT_LEAST8_MIN    (-INT_LEAST8_MAX - 1)
#define INT_LEAST16_MIN   (-INT_LEAST16_MAX - 1)
#define INT_LEAST32_MIN   (-INT_LEAST32_MAX - 1)
#define INT_LEAST64_MIN   (-INT_LEAST64_MAX - 1)

typedef __UINT_FAST8_TYPE__     uint_fast8_t;
typedef __UINT_FAST16_TYPE__    uint_fast16_t;
typedef __UINT_FAST32_TYPE__    uint_fast32_t;
typedef __UINT_FAST64_TYPE__    uint_fast64_t;
typedef __INT_FAST8_TYPE__      int_fast8_t;
typedef __INT_FAST16_TYPE__     int_fast16_t;
typedef __INT_FAST32_TYPE__     int_fast32_t;
typedef __INT_FAST64_TYPE__     int_fast64_t;

#define UINT_FAST8_MAX    __UINT_FAST8_MAX__
#define UINT_FAST16_MAX   __UINT_FAST16_MAX__
#define UINT_FAST32_MAX   __UINT_FAST32_MAX__
#define UINT_FAST64_MAX   __UINT_FAST64_MAX__

#define INT_FAST8_MAX    __INT_FAST8_MAX__
#define INT_FAST16_MAX   __INT_FAST16_MAX__
#define INT_FAST32_MAX   __INT_FAST32_MAX__
#define INT_FAST64_MAX   __INT_FAST64_MAX__

#define INT_FAST8_MIN    (-INT_FAST8_MAX - 1)
#define INT_FAST16_MIN   (-INT_FAST16_MAX - 1)
#define INT_FAST32_MIN   (-INT_FAST32_MAX - 1)
#define INT_FAST64_MIN   (-INT_FAST64_MAX - 1)

typedef __INTPTR_TYPE__   intptr_t;
typedef __UINTPTR_TYPE__  uintptr_t;

#define INTPTR_MAX        __INTPTR_MAX__
#define INTPTR_MIN        (-INTPTR_MAX - 1)
#define UINTPTR_MAX       __UINTPTR_MAX__

typedef __INTMAX_TYPE__   intmax_t;
typedef __UINTMAX_TYPE__  uintmax_t;

#define INTMAX_MAX        __INTMAX_MAX__
#define INTMAX_MIN        (-INTMAX_MAX - 1)
#define UINTMAX_MAX       __UINTMAX_MAX__

#define SIZE_MAX          __SIZE_MAX__

// GCC defines __<type>INT<size>_C macros which append the correct suffix
// to an (un)signed integer literal, with <size> being one of 8, 16, 32, 64
// and MAX, and type being U for unsigned types. The standard stdint.h macros
// with the same names without the leading __ could be implemented in terms of
// these macros.
//
// Clang predefines macros __<type>INT<size>_C_SUFFIX__ which expand to the
// suffix itself. We define the standard stdint.h macros in terms of the GCC
// variants and for Clang we define their equivalents.
#ifndef __INTMAX_C

#define __int_c_join(a, b) a ## b
#define __int_c(v, suffix) __int_c_join(v, suffix)

#define __INT8_C(c)       __int_c(c, __INT8_C_SUFFIX__)
#define __INT16_C(c)      __int_c(c, __INT16_C_SUFFIX__)
#define __INT32_C(c)      __int_c(c, __INT32_C_SUFFIX__)
#define __INT64_C(c)      __int_c(c, __INT64_C_SUFFIX__)

#define __UINT8_C(c)      __int_c(c, __UINT8_C_SUFFIX__)
#define __UINT16_C(c)     __int_c(c, __UINT16_C_SUFFIX__)
#define __UINT32_C(c)     __int_c(c, __UINT32_C_SUFFIX__)
#define __UINT64_C(c)     __int_c(c, __UINT64_C_SUFFIX__)

#define __INTMAX_C(c)     __int_c(c, __INTMAX_C_SUFFIX__)
#define __UINTMAX_C(c)    __int_c(c, __UINTMAX_C_SUFFIX__)

#endif

#define INT8_C(c)         __INT8_C(c)
#define INT16_C(c)        __INT16_C(c)
#define INT32_C(c)        __INT32_C(c)
#define INT64_C(c)        __INT64_C(c)

#define UINT8_C(c)        __UINT8_C(c)
#define UINT16_C(c)       __UINT16_C(c)
#define UINT32_C(c)       __UINT32_C(c)
#define UINT64_C(c)       __UINT64_C(c)

#define INTMAX_C(c)       __INTMAX_C(c)
#define UINTMAX_C(c)      __UINTMAX_C(c)
