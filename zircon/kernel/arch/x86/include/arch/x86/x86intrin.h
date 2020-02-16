// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_X86INTRIN_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_X86INTRIN_H_

// TODO(mcgrathr): As of GCC 6.3.0, these other files included by
// <x86intrin.h> are incompatible with -mno-sse.
// When https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80298 is fixed,
// these #define hacks can be removed.
#if !defined(__clang__) && __GNUC__ < 7
#define _AVX512VLINTRIN_H_INCLUDED
#define _AVX512BWINTRIN_H_INCLUDED
#define _AVX512DQINTRIN_H_INCLUDED
#define _AVX512VLBWINTRIN_H_INCLUDED
#define _AVX512VLDQINTRIN_H_INCLUDED
#define _AVX512VBMIINTRIN_H_INCLUDED
#define _AVX512VBMIVLINTRIN_H_INCLUDED
#define _MM3DNOW_H_INCLUDED
#define _FMA4INTRIN_H_INCLUDED
#define _XOPMMINTRIN_H_INCLUDED
#endif

#if !defined(__clang__)
// GCC's <x86intrin.h> indirectly includes its <mm_malloc.h>, which
// is useless to us but requires an <errno.h> with declarations.
#define _MM_MALLOC_H_INCLUDED
#endif

#include <x86intrin.h>

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_X86INTRIN_H_
