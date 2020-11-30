// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef LIB_STATIC_PIE_STATIC_PIE_H_
#define LIB_STATIC_PIE_STATIC_PIE_H_

#include <cstdint>

namespace static_pie {
struct Elf64DynamicEntry;
}

// Apply relocations specified in the given `.dynamic` table to the
// currently running binary, assuming that we have been loaded in at
// `load_address`.
extern "C" void ApplyDynamicRelocations(const static_pie::Elf64DynamicEntry* dynamic_table,
                                        uintptr_t load_address);

#endif  // LIB_STATIC_PIE_STATIC_PIE_H_
