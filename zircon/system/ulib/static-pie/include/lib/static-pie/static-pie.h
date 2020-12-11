// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef LIB_STATIC_PIE_STATIC_PIE_H_
#define LIB_STATIC_PIE_STATIC_PIE_H_

#include <stdint.h>
#include <zircon/compiler.h>

namespace static_pie {

// Apply relocations specified in the running executable's `.dynamic`
// table to the currently running binary, assuming that we have been
// loaded in at `load_address`.
extern "C" void ApplyDynamicRelocationsToSelf(uintptr_t load_address);

}  // namespace static_pie

#endif  // LIB_STATIC_PIE_STATIC_PIE_H_
