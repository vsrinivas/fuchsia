// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_KTL_INCLUDE_WCHAR_H_
#define ZIRCON_KERNEL_LIB_KTL_INCLUDE_WCHAR_H_

#include <stddef.h>

// The kernel doesn't want this file but some libc++ headers we need
// wind up including it and they need these declarations.

typedef struct {
} mbstate_t;

typedef unsigned int wint_t;
#define WEOF (~wint_t{0})

// <string_view> refers to these in code that's never actually used in ktl.
wchar_t* wmemcpy(wchar_t* __restrict, const wchar_t* __restrict, size_t);
wchar_t* wmemmove(wchar_t*, const wchar_t*, size_t);
wchar_t* wmemset(wchar_t*, wchar_t, size_t);

#endif  // ZIRCON_KERNEL_LIB_KTL_INCLUDE_WCHAR_H_
