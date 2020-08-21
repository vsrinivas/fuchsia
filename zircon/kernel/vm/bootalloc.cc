// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vm/bootalloc.h"

#include <align.h>
#include <lib/instrumentation/asan.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <trace.h>

#include <vm/physmap.h>
#include <vm/pmm.h>
#include <vm/vm.h>

#include "vm_priv.h"

#define LOCAL_TRACE VM_GLOBAL_TRACE(0)

// Simple boot time allocator that starts by allocating physical memory off
// the end of wherever the kernel is loaded in physical space.
//
// Pointers are returned from the kernel's physmap

// store the start and current pointer to the boot allocator in physical address
paddr_t boot_alloc_start;
paddr_t boot_alloc_end;

// run in physical space without the mmu set up, so by computing the address of _end
// and saving it, we've effectively computed the physical address of the end of the
// kernel.
// We can't allow asan to check the globals here as it happens on a different
// aspace where asan shadow isn't mapped.
NO_ASAN __NO_SAFESTACK void boot_alloc_init() {
  boot_alloc_start = reinterpret_cast<paddr_t>(_end);
  // TODO(ZX-2563): This is a compile-time no-op that defeats any compiler
  // optimizations based on its knowledge/assumption that `&_end` is a
  // constant here that equals the `&_end` constant as computed elsewhere.
  // Without this, the compiler can see that boot_alloc_start is never set to
  // any other value and replace code that uses the boot_alloc_start value
  // with code that computes `&_end` on the spot.  What the compiler doesn't
  // know is that this `&_end` is crucially a PC-relative computation when
  // the PC is a (low) physical address.  Later code that uses
  // boot_alloc_start will be running at a kernel (high) virtual address and
  // so its `&_end` will be nowhere near the same value.  The compiler isn't
  // wrong to do this sort of optimization when it can and other such cases
  // will eventually arise.  So long-term we'll need more thorough
  // compile-time separation of the early boot code that runs in physical
  // space from normal kernel code.  For now, this asm generates no
  // additional code but tells the compiler that it has no idea what value
  // boot_alloc_start might take, so it has to compute the `&_end` value now.
  __asm__("" : "=g"(boot_alloc_start) : "0"(boot_alloc_start));
  boot_alloc_end = reinterpret_cast<paddr_t>(_end);
}

void boot_alloc_reserve(paddr_t start, size_t len) {
  uintptr_t end = ALIGN((start + len), PAGE_SIZE);

  if (end >= boot_alloc_start) {
    if ((start > boot_alloc_start) && ((start - boot_alloc_start) > (128 * 1024 * 1024))) {
      // if we've got 128MB of space, that's good enough
      // it's possible that the start may be *way* far up
      // (gigabytes) and there may not be space after it...
      return;
    }
    boot_alloc_start = boot_alloc_end = end;
  }
}

void* boot_alloc_mem(size_t len) {
  uintptr_t ptr;

  ptr = ALIGN(boot_alloc_end, 8);
  boot_alloc_end = (ptr + ALIGN(len, 8));

  LTRACEF("len %zu, phys ptr %#" PRIxPTR " ptr %p\n", len, ptr, paddr_to_physmap(ptr));

  return paddr_to_physmap(ptr);
}

// called from arch start.S
// run in physical space without the mmu set up, so stick to basic, relocatable code
__NO_SAFESTACK
paddr_t boot_alloc_page_phys() {
  paddr_t ptr = ALIGN(boot_alloc_end, PAGE_SIZE);
  boot_alloc_end = ptr + PAGE_SIZE;

  return ptr;
}
