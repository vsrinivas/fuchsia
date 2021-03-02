// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "mmu.h"

#include <zircon/types.h>

#include "bits.h"

namespace page_table::x86 {

bool PageTableEntry::is_page(int8_t level) const {
  // Top level doesn't support pages.
  if (level == kPageTableLevels - 1) {
    return false;
  }
  // Bottom level only supports pages.
  if (level == 0) {
    return true;
  }
  return ExtractBit(kPageBitIndex.at(level), raw);
}

PageTableEntry& PageTableEntry::set_is_page(int8_t level, bool value) {
  // Top level doesn't support pages.
  if (level == kPageTableLevels - 1) {
    ZX_DEBUG_ASSERT(!value);
    return *this;
  }
  // Bottom level only supports pages.
  if (level == 0) {
    ZX_DEBUG_ASSERT(value);
    return *this;
  }
  raw = SetBit(kPageBitIndex.at(level), /*word=*/raw, /*bit=*/value ? 1 : 0);
  return *this;
}

uint64_t PageTableEntry::pat(int8_t level) const {
  int index = kPatBitIndex.at(level);
  ZX_DEBUG_ASSERT(index >= 0);
  return ExtractBit(index, raw);
}

PageTableEntry& PageTableEntry::set_pat(int8_t level, uint64_t value) {
  int index = kPatBitIndex.at(level);
  ZX_DEBUG_ASSERT(index >= 0);
  raw = SetBit(/*index=*/index, /*word=*/raw, /*bit=*/value);
  return *this;
}

uint64_t PageTableEntry::page_paddr(int8_t level) const {
  return ExtractBits(kPhysAddressBits, PageLevelBits(level), raw) << PageLevelBits(level);
}

PageTableEntry& PageTableEntry::set_page_paddr(int8_t level, uint64_t value) {
  ZX_DEBUG_ASSERT(value % (1 << PageLevelBits(level)) == 0);
  raw = ClearBits(/*high=*/kPhysAddressBits, /*low=*/PageLevelBits(level), raw) | value;
  return *this;
}

}  // namespace page_table::x86
