// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vm/pmm_checker.h"

#include <assert.h>
#include <lib/cmdline.h>
#include <platform.h>
#include <sys/types.h>

#include <vm/physmap.h>

namespace {

// The value 0x43 was chosen because it stands out when interpreted as ASCII ('C') and is an odd
// value that is less likely to occur natually (e.g. arm64 instructions are 4-byte aligned).
constexpr uint8_t kPatternOneByte = 0x43u;
constexpr uint64_t kPattern = 0x4343434343434343ull;

}  // namespace

void PmmChecker::Arm() { armed_ = true; }

void PmmChecker::Disarm() { armed_ = false; }

void PmmChecker::FillPattern(vm_page_t* page) {
  DEBUG_ASSERT(page->is_free());
  void* kvaddr = paddr_to_physmap(page->paddr());
  DEBUG_ASSERT(is_kernel_address(reinterpret_cast<vaddr_t>(kvaddr)));
  memset(kvaddr, kPatternOneByte, PAGE_SIZE);
}

bool PmmChecker::ValidatePattern(vm_page_t* page) {
  if (!armed_) {
    return true;
  }

  // Validate the pattern.  There's a decent chance that, on arm64, checking 8 bytes at a time will
  // be faster than 1 byte at time.
  auto kvaddr = static_cast<uint64_t*>(paddr_to_physmap(page->paddr()));
  for (size_t j = 0; j < PAGE_SIZE / 8; ++j) {
    if (kvaddr[j] != kPattern) {
      return false;
    }
  }
  return true;
}

static void DumpPageAndPanic(vm_page_t* page) {
  platform_panic_start();
  auto kvaddr = static_cast<void*>(paddr_to_physmap(page->paddr()));
  printf("pmm checker found unexpected pattern in page at %p\n", kvaddr);
  printf("dump of page follows\n");
  hexdump8(kvaddr, PAGE_SIZE);
  panic("pmm corruption suspected\n" );
}

void PmmChecker::AssertPattern(vm_page_t* page) {
  if (!ValidatePattern(page)) {
    DumpPageAndPanic(page);
  }
}
