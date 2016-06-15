// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// use the GCC builtins until we need something ourselves
#if !defined(_GCC_LIMITS_H_)
#include_next <limits.h>
#endif

#ifndef LLONG_MAX
#define LLONG_MAX ((1LL << (CHAR_BIT * sizeof(long long) - 1)) - 1)
#endif
#ifndef ULLONG_MAX
#define ULLONG_MAX (~0ULL)
#endif
#ifndef LLONG_MIN
#define LLONG_MIN (LLONG_MAX + 1)
#endif
