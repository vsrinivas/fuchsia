// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/defines.h>
#include <zircon/compiler.h>

// Use to align structures on cache lines to avoid cpu aliasing.
#define __CPU_ALIGN __ALIGNED(MAX_CACHE_LINE)

// Similar to above, but put in a special section to make sure no other
// variable shares the same cache line.
#define __CPU_ALIGN_EXCLUSIVE __CPU_ALIGN __SECTION(".data.cpu_align_exclusive")
