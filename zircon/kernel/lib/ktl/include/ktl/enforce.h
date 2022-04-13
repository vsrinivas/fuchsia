// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_ENFORCE_H_
#define ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_ENFORCE_H_

// `#include <ktl/enforce.h>` should appear last in the include list of any
// kernel .cc file that uses `ktl` headers.  Since many other kernel headers
// use ktl headers and this must appear after *all* uses of any ktl headers,
// whether direct or indirect, it's a good idea to just make sure that
// `#include <ktl/enforce.h>` in each .cc file of kernel-only code (i.e. code
// that might ever use ktl directly).
//
// The //.clang-format file has a rule to ensure this file is placed last and
// separate from the other #include lines, so just adding the `#include` line
// to each source file should make sure its placement stays correct.

// This directive tells the preprocessor that the identifier `std` cannot
// appear in any context (except the expansion of a macro already defined
// before now).  This ensures that any stray use of std::name in kernel code
// can get caught at compile time so people remember to use ktl::name instead
// and ensure that what they're using is in the approved ktl subset of std.
//
// It also affects harmless use of `std` as a local variable name or whatever,
// but we can live without kernel code being able to use that one name.
#pragma GCC poison std

// Also poison the stdcompat namespaces, as the same rules for ktl wrappers
// apply to those as std proper.
#pragma GCC poison cpp17
#pragma GCC poison cpp20
#pragma GCC poison cpp23

#endif  // ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_ENFORCE_H_
