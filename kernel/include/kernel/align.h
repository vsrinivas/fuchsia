// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/defines.h>

// Use to align structures on cache lines to avoid cpu aliasing.
#define __CPU_ALIGN __ALIGNED(MAX_CACHE_LINE)
