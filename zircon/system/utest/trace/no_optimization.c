// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This testcase is mainly a compilation test: Do we get undefined symbols
// when we link C code compiled with -O0? The semantics of inline functions
// are different between C and C++, and with C at -O0 we need to verify a
// real copy of the functions exist (unless they get marked __ALWAYS_INLINE).

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O0")
#endif
#ifdef __clang__
#pragma clang optimize off
#endif

#define NO_OPTIM
#include "event_tests_common.h"

