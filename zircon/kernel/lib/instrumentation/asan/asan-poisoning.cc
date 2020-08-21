// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <platform.h>
#include <string.h>
#include <zircon/assert.h>

#include <ktl/algorithm.h>
#include <ktl/atomic.h>
#include <ktl/span.h>
#include <sanitizer/asan_interface.h>
#include <vm/pmm.h>

#include "asan-internal.h"

ktl::atomic<bool> g_asan_initialized;

namespace {

// Checks if an entire memory region is all zeroes.
bool is_mem_zero(ktl::span<const uint8_t> region) {
  for (auto val : region) {
    if (val != 0) {
      return false;
    }
  }
  return true;
}

// When kASAN has detected an invalid access, print information about the access and the
// corresponding parts of the shadow map. Also print PMM page state.
//
// Example:
// (Shadow address)        (shadow map contents)
//
// KASAN detected a write error: ptr={{{data:0xffffff8043251830}}}, size=0x4, caller:
// {{{pc:0xffffffff001d9371}}} Shadow memory state around the buggy address 0xffffffe00864a306:
// 0xffffffe00864a2f0: 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa
// 0xffffffe00864a2f8: 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa
// 0xffffffe00864a300: 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa
//                                                    ^^
// 0xffffffe00864a308: 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa
// 0xffffffe00864a310: 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa
// page 0xffffff807f475f30: address 0x43251000 state heap flags 0
void print_error_shadow(uintptr_t address, size_t bytes, bool is_write, void* caller,
                        uintptr_t poisoned_addr) {
  const uintptr_t shadow = reinterpret_cast<uintptr_t>(addr2shadow(address));

  dprintf(CRITICAL,
          "\nKASAN detected a %s error: ptr={{{data:%#lx}}}, size=%#zx, caller: {{{pc:%p}}}\n",
          !is_write ? "read" : "write", address, bytes, caller);

  // TODO(fxbug.dev/30033): Decode the shadow value into 'use-after-free'/redzone/page free/etc.
  printf("Shadow memory state around the buggy address %#lx:\n", shadow);
  // Print at least 16 bytes of the shadow map before and after the invalid access.
  uintptr_t start_addr = (shadow & ~0x07) - 0x10;
  start_addr = ktl::max(KASAN_SHADOW_OFFSET, start_addr);
  // Print the shadow map memory state and look for the location to print a caret.
  bool caret = false;
  size_t caret_ind = 0;
  for (size_t i = 0; i < 5; i++) {
    // TODO(fxbug.dev/51170): When kernel printf properly supports #, switch.
    printf("0x%016lx:", start_addr);
    for (size_t j = 0; j < 8; j++) {
      printf(" 0x%02hhx", reinterpret_cast<uint8_t*>(start_addr)[j]);
      if (!caret) {
        if ((start_addr + j) == reinterpret_cast<uintptr_t>(addr2shadow(poisoned_addr))) {
          caret = true;
          caret_ind = j;
        }
      }
    }
    printf("\n");
    if (caret) {
      // The address takes 16 characters; add in space for ':', and "0x".
      printf("%*s", 16 + 1 + 2, "");
      // Print either a caret or spaces under the line containing the invalid access.
      for (size_t j = 0; j < 8; j++) {
        printf("  %2s ", j == caret_ind ? "^^" : "");
      }
      printf("\n");
      caret = false;
    }
    start_addr += 8;
  }
  // Dump additional VM Page state - this is useful to debug use-after-state-change bugs.
  paddr_to_vm_page(vaddr_to_paddr(reinterpret_cast<void*>(address)))->dump();
}

inline bool RangesOverlap(uintptr_t offset1, size_t len1, uintptr_t offset2, size_t len2) {
  return !((offset1 + len1 <= offset2) || (offset2 + len2 <= offset1));
}

}  // namespace

namespace asan {

void* Quarantine::push(void* allocation) {
  auto& slot = queue_[pos_++ % kQuarantineElements];
  void* const result = slot;
  slot = allocation;
  return result;
}

}  // namespace asan

// Checks whether a memory |address| is poisoned.
//
// ASAN tracks address poison status at byte granularity in a shadow map.
// kAsanGranularity bytes are represented by one byte in the shadow map.
//
// If the value in the shadow map is 0, accesses to address are allowed.
// If the value is in [1, kAsanGranularity), accesses to the corresponding
// addresses less than the value are allowed.
// All other values disallow access to the entire aligned region.
bool asan_address_is_poisoned(uintptr_t address) {
  const uint8_t shadow_val = *addr2shadow(address);
  // Zero values in the shadow map mean that all 8 bytes are valid.
  if (shadow_val == 0) {
    return false;
  }
  if (shadow_val >= kAsanSmallestPoisonedValue) {
    return true;
  }
  // Part of this region is poisoned. Check whether address is below the last valid byte.
  const size_t offset = address & kAsanGranularityMask;
  return shadow_val <= offset;
}

bool asan_entire_region_is_poisoned(uintptr_t address, size_t size) {
  for (size_t i = 0; i < size; i++) {
    if (!asan_address_is_poisoned(address + i)) {
      return false;
    }
  }
  return true;
}

uintptr_t asan_region_is_poisoned(uintptr_t address, size_t size) {
  const uintptr_t end = address + size;
  const uintptr_t aligned_begin = ROUNDUP(address, kAsanGranularity);
  const uintptr_t aligned_end = ROUNDDOWN(end, kAsanGranularity);
  const uint8_t* const shadow_beg = addr2shadow(aligned_begin);
  const uint8_t* const shadow_end = addr2shadow(aligned_end);

  if (!asan_address_is_poisoned(address) && !asan_address_is_poisoned(end - 1) &&
      (shadow_end <= shadow_beg ||
       is_mem_zero({shadow_beg, static_cast<size_t>(shadow_end - shadow_beg)}))) {
    return 0;
  }

  for (size_t i = 0; i < size; i++) {
    if (asan_address_is_poisoned(address + i)) {
      return address + i;
    }
  }
  panic("Unreachable code\n");
}

void asan_check(uintptr_t address, size_t bytes, bool is_write, void* caller) {
  // TODO(fxbug.dev/30033): Inline the fast path for constant-size checks.
  const uintptr_t poisoned_addr = asan_region_is_poisoned(address, bytes);
  if (!poisoned_addr) {
    return;
  }
  platform_panic_start();
  print_error_shadow(address, bytes, is_write, caller, poisoned_addr);
  panic("kasan\n");
}

void asan_check_memory_overlap(uintptr_t offset1, size_t len1, uintptr_t offset2, size_t len2) {
  if (!RangesOverlap(offset1, len1, offset2, len2))
    return;
  platform_panic_start();
  printf("KASAN detected a memory range overlap error.\n");
  printf("ptr: 0x%016lx size: %#zx overlaps with ptr: 0x%016lx size: %#zx\n", offset1, len1,
         offset2, len2);
  panic("kasan\n");
}

void asan_poison_shadow(uintptr_t address, size_t size, uint8_t value) {
  // pmm_alloc_page is called before the kasan shadow map has been remapped r/w.
  // Do not attempt to poison memory in that case.
  if (!g_asan_initialized.load()) {
    return;
  }
  DEBUG_ASSERT(size > 0);
  DEBUG_ASSERT(value >= kAsanSmallestPoisonedValue);  // only used for poisoning.

  uint8_t* shadow_addr_beg = addr2shadow(address);
  uint8_t* const shadow_addr_end = addr2shadow(address + size);

  const uint8_t offset = address & kAsanGranularityMask;
  const uint8_t end_offset = (address + size) & kAsanGranularityMask;

  // We want to poison less than one shadow byte. This shadow byte
  // could be: Unpoisoned, Poisoned or Partially Poisoned.
  // For the poisoned case, we avoid repoisoning.
  // For the unpoisoned and partially poisoned cases, we can't leave gaps:
  // only poison if the last byte (end_offset) is already poisoned. In that
  // case, this might end up poisoning the entire byte, or could extend the
  // current partially poisoned area.
  if (shadow_addr_beg == shadow_addr_end) {
    // If the shadow byte is already poisoned, do nothing.
    if (shadow_addr_beg[0] >= kAsanSmallestPoisonedValue)
      return;
    // If the shadow byte is unpoisoned, do nothing.
    if (shadow_addr_beg[0] == 0)
      return;
    // If the byte at address+size is not poisoned, do nothing: otherwise
    // there would be an unpoisoned gap.
    if (shadow_addr_beg[0] > end_offset)
      return;

    if (offset != 0) {
      // Partially poison the shadow byte. Only override if we are expanding
      // the poisoned area.
      shadow_addr_beg[0] = ktl::min(shadow_addr_beg[0], offset);
    } else {
      // Poison the entire byte.
      shadow_addr_beg[0] = value;
    }
    return;
  }

  // We need to check whether we are partially poisoning the first shadow
  // byte, but only if it is not already poisoned.
  if (offset != 0) {
    if (shadow_addr_beg[0] == 0) {
      shadow_addr_beg[0] = offset;
    } else if (shadow_addr_beg[0] < kAsanSmallestPoisonedValue) {
      // value is partially poisoned, only override if we are poisoning more.
      shadow_addr_beg[0] = ktl::min(shadow_addr_beg[0], offset);
    }
    shadow_addr_beg += 1;
    address += offset;
    size -= offset;
  }

  __unsanitized_memset(shadow_addr_beg, value, shadow_addr_end - shadow_addr_beg);

  // If the last shadow byte (shadow_addr_end) is partially poisoned, we might
  // be completing the whole shadow byte, otherwise, don't touch it.
  // For example, if the last byte has 2 bytes unpoisoned, but our end_offset is
  // 3, it means that we can safely poison the entire shadow byte.
  if (end_offset != 0) {
    if (shadow_addr_end[0] > 0 && shadow_addr_end[0] <= end_offset) {
      shadow_addr_end[0] = value;
    }
  }
}

void asan_unpoison_shadow(uintptr_t address, size_t size) {
  // pmm_alloc_page is called before the kasan shadow map has been remapped r/w.
  // Do not attempt to unpoison memory in that case.
  if (!g_asan_initialized.load()) {
    return;
  }
  DEBUG_ASSERT(size > 0);
  uint8_t* const shadow_addr_beg = addr2shadow(address);
  uint8_t* const shadow_addr_end = addr2shadow(address + size);

  __unsanitized_memset(shadow_addr_beg, 0, shadow_addr_end - shadow_addr_beg);

  // The last shadow byte should capture how many bytes
  // are unpoisoned at the end of the poisoned area.
  const uint8_t end_offset = (address + size) & kAsanGranularityMask;
  if (end_offset != 0) {
    if (shadow_addr_end[0] >= kAsanSmallestPoisonedValue) {
      // The entire byte is poisoned, unpoison up until end_offset.
      shadow_addr_end[0] = end_offset;
    } else if (shadow_addr_end[0] != 0) {
      // The byte is partially poisoned. See if we can increase the unpoisoned
      // region.
      shadow_addr_end[0] = ktl::max(shadow_addr_end[0], end_offset);
    }
  }
}

size_t asan_heap_redzone_size(size_t size) {
  // The allocation end might not be aligned to an asan granule, so we add
  // the remaining part to the redzone size so that size + redzone_size
  // is aligned to an asan granule.
  size_t remaining = ROUNDUP(size, kAsanGranularity) - size;
  return kHeapRightRedzoneSize + remaining;
}
