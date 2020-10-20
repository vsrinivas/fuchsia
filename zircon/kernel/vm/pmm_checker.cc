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

#include "debug.h"

namespace {

// The value 0x43 was chosen because it stands out when interpreted as ASCII ('C') and is an odd
// value that is less likely to occur natually (e.g. arm64 instructions are 4-byte aligned).
constexpr uint8_t kPatternOneByte = 0x43u;
constexpr uint64_t kPattern = 0x4343434343434343ull;

// This is a macro because it's passed to KERNEL_OOPS and KERNEL_OOPS requires a literal.
#define FORMAT_MESSAGE "pmm checker found unexpected pattern in page at %p; fill size is %lu\n"

void DumpPage(size_t fill_size, void* kvaddr) {
  printf("dump of page follows\n");
  hexdump8(kvaddr, PAGE_SIZE);
}

void DumpPageAndOops(vm_page_t* page, size_t fill_size, void* kvaddr) {
  KERNEL_OOPS(FORMAT_MESSAGE, kvaddr, fill_size);
  DumpPage(fill_size, kvaddr);
}

void DumpPageAndPanic(vm_page_t* page, size_t fill_size, void* kvaddr) {
  platform_panic_start();
  printf(FORMAT_MESSAGE, kvaddr, fill_size);
  DumpPage(fill_size, kvaddr);
  panic("pmm free list corruption suspected\n");
}

}  // namespace

// static
ktl::optional<PmmChecker::Action> PmmChecker::ActionFromString(const char* action_string) {
  if (!strcmp(action_string, "oops")) {
    return PmmChecker::Action::OOPS;
  }
  if (!strcmp(action_string, "panic")) {
    return PmmChecker::Action::PANIC;
  }
  return ktl::nullopt;
}

// static
const char* PmmChecker::ActionToString(Action action) {
  switch (action) {
    case PmmChecker::Action::OOPS:
      return "oops";
    case PmmChecker::Action::PANIC:
      return "panic";
  };
  __UNREACHABLE;
}

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

void PmmChecker::AssertPattern(vm_page_t* page) {
  if (!ValidatePattern(page)) {
    auto kvaddr = static_cast<void*>(paddr_to_physmap(page->paddr()));
    switch (action_) {
      case Action::OOPS:
        DumpPageAndOops(page, fill_size_, kvaddr);
        break;
      case Action::PANIC:
        DumpPageAndPanic(page, fill_size_, kvaddr);
        break;
    }
  }
}

void PmmChecker::PrintStatus(FILE* f) const {
  fprintf(f, "PMM: pmm checker %s, fill size is %lu, action is %s\n",
          armed_ ? "enabled" : "disabled", fill_size_, ActionToString(action_));
}
