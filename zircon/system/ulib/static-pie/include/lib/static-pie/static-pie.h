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

struct Elf64DynamicEntry;

// Beginning of the ELF ".dynamic" section.
extern "C" __LOCAL const Elf64DynamicEntry _DYNAMIC[];

// Apply relocations specified in the given `.dynamic` table to the
// currently running binary, assuming that we have been loaded in at
// `load_address`.
extern "C" void ApplyDynamicRelocations(const Elf64DynamicEntry* dynamic_table,
                                        uintptr_t load_address);

}  // namespace static_pie

#endif  // LIB_STATIC_PIE_STATIC_PIE_H_
