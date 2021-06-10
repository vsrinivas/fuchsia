// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_UNWINDER_DWARF_CFI_H_
#define SRC_DEVELOPER_DEBUG_UNWINDER_DWARF_CFI_H_

#include <cstdint>
#include <map>

#include "src/developer/debug/unwinder/error.h"
#include "src/developer/debug/unwinder/memory.h"
#include "src/developer/debug/unwinder/registers.h"

namespace unwinder {

// Load the DWARF Call Frame Information from the .eh_frame section of an ELF image.
//
// TODO(dangyi): support reading from .debug_frame section for -fno-unwind-table binaries.
//
// This class doesn't cache the memory so if repeated lookups are required, it's recommended to use
// a cached Memory implementation.
class DwarfCfi {
 public:
  // Load the eh_frame from the ELF file.
  //
  // This function only reads the eh_frame_hdr and loads the address of the binary search table
  // to avoid using any significant memory for large binaries.
  [[nodiscard]] Error Load(Memory* elf, uint64_t elf_ptr);

  // Unwind one frame.
  [[nodiscard]] Error Step(Memory* stack, const Registers& current, Registers& next);

 private:
  Memory* elf_ = nullptr;
  uint64_t eh_frame_hdr_ptr_ = 0;

  // Marks the executable section so that we don't need to find the FDE to know a PC is wrong.
  uint64_t pc_begin_ = 0;
  uint64_t pc_end_ = 0;

  // eh_frame_hdr binary search table info
  uint64_t fde_count_ = 0;         // Number of entries in the binary search table.
  uint64_t table_ptr_ = 0;         // Pointer to the binary search table.
  uint8_t table_enc_ = 0;          // Encoding for pointers in the table.
  uint64_t table_entry_size_ = 0;  // Size of each entry in the table.
};

}  // namespace unwinder

#endif  // SRC_DEVELOPER_DEBUG_UNWINDER_DWARF_CFI_H_
