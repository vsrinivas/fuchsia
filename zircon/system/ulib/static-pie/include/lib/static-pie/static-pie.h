// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STATIC_PIE_STATIC_PIE_H_
#define LIB_STATIC_PIE_STATIC_PIE_H_

#include <stdint.h>
#include <zircon/compiler.h>

namespace static_pie {

// Apply relocations specified in the running executable's `.dynamic`
// table to the currently running binary.
//
// `load_address` should contain the address that the executable's first LOAD
// segment was loaded.
//
// `link_address` should contain the address that the executable's first LOAD
// segment was linked at. For position-independent binaries, this will often
// be 0.
extern "C" void ApplyDynamicRelocationsToSelf(uintptr_t link_address, uintptr_t load_address);

}  // namespace static_pie

#endif  // LIB_STATIC_PIE_STATIC_PIE_H_
