// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/assert.h>

#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr) ZX_ASSERT(expr)
#endif
