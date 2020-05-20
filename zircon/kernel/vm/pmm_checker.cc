// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vm/pmm_checker.h"

#include <assert.h>
#include <lib/cmdline.h>
#include <lib/instrumentation/asan.h>
#include <platform.h>
#include <string.h>
#include <sys/types.h>

#include <pretty/hexdump.h>
#include <vm/physmap.h>

namespace {

// The value 0x43 was chosen because it stands out when interpreted as ASCII ('C') and is an odd
// value that is less likely to occur natually (e.g. arm64 instructions are 4-byte aligned).
constexpr uint8_t kPatternOneByte = 0x43u;
constexpr uint64_t kPattern = 0x4343434343434343ull;

}  // namespace

// static
bool PmmChecker::IsValidFillSize(size_t fill_size) {
  return fill_size >= 8 && fill_size <= PAGE_SIZE && (fill_size % 8 == 0);
}

void PmmChecker::SetFillSize(size_t fill_size) {
  DEBUG_ASSERT(IsValidFillSize(fill_size));
  DEBUG_ASSERT(!armed_);
  fill_size_ = fill_size;
}

void PmmChecker::Arm() { armed_ = true; }

void PmmChecker::Disarm() { armed_ = false; }

void PmmChecker::FillPattern(vm_page_t* page) {
  DEBUG_ASSERT(page->is_free());
  void* kvaddr = paddr_to_physmap(page->paddr());
  DEBUG_ASSERT(is_kernel_address(reinterpret_cast<vaddr_t>(kvaddr)));
  __unsanitized_memset(kvaddr, kPatternOneByte, fill_size_);
}

NO_ASAN bool PmmChecker::ValidatePattern(vm_page_t* page) {
  if (!armed_) {
    return true;
  }

  // Validate the pattern.  There's a decent chance that, on arm64, checking 8 bytes at a time will
  // be faster than 1 byte at time.
  auto kvaddr = static_cast<uint64_t*>(paddr_to_physmap(page->paddr()));
  for (size_t j = 0; j < fill_size_ / 8; ++j) {
    if (kvaddr[j] != kPattern) {
      return false;
    }
  }
  return true;
}

static void DumpPageAndPanic(vm_page_t* page, size_t fill_size) {
  platform_panic_start();
  auto kvaddr = static_cast<void*>(paddr_to_physmap(page->paddr()));
  printf("pmm checker found unexpected pattern in page at %p; fill size is %lu\n", kvaddr,
         fill_size);
  printf("dump of page follows\n");
  // Regardless of fill size, dump the whole page since it may prove useful for debugging.
  hexdump8(kvaddr, PAGE_SIZE);
  panic("pmm corruption suspected\n");
}

void PmmChecker::AssertPattern(vm_page_t* page) {
  if (!ValidatePattern(page)) {
    DumpPageAndPanic(page, fill_size_);
  }
}
