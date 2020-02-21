// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "trampoline.h"

#include <inttypes.h>
#include <stddef.h>
#include <zircon/compiler.h>

// Populate the trampoline area and enter the kernel in 64-bit mode.  Paging is
// already enabled.  The page tables, the ZBI image (which includes the kernel
// item), and the trampoline area are all placed safely outside the kernel's
// range: PHYS_LOAD_ADDRESS + kernel image size + kernel bss size.
noreturn void boot_zbi(const zbi_header_t* zbi, const zbi_header_t* kernel_item,
                       struct trampoline* trampoline) {
  // The kernel image includes its own container and item headers.
  const size_t kernel_size = kernel_item->length + (2 * sizeof(zbi_header_t));

  // The header inside the kernel item payload gives the entry point as an
  // absolute physical address.
  const zbi_kernel_t* kernel_header = (void*)(kernel_item + 1);
  uint32_t kernel_entry = kernel_header->entry;
  if (unlikely(kernel_entry != kernel_header->entry)) {
    panic("ZBI kernel entry point %#llx truncated to %#" PRIx32, kernel_header->entry,
          kernel_entry);
  }
  if (unlikely(kernel_entry < (uintptr_t)PHYS_LOAD_ADDRESS ||
               kernel_entry >= (uintptr_t)PHYS_LOAD_ADDRESS + kernel_size)) {
    panic("ZBI kernel entry point %#" PRIx32 " outside kernel [%p, %p)", kernel_entry,
          PHYS_LOAD_ADDRESS, PHYS_LOAD_ADDRESS + kernel_size);
  }

  // The headers matter for the address arithmetic of where the image gets
  // placed.  But the kernel doesn't actually look at those headers, so they
  // don't need to be filled in.
  const uint8_t* copy_src = (const void*)(kernel_header + 1);
  uint8_t* copy_dest = PHYS_LOAD_ADDRESS + offsetof(zircon_kernel_t, contents);
  const size_t copy_size = kernel_item->length;

  // The descriptor needed to load the new GDT can be placed on the stack.
  const struct {
    uint16_t limit;
    void* base;
  } __PACKED lgdt = {
      .base = trampoline->gdt,
      .limit = sizeof(trampoline->gdt) - 1,
  };

  // The trampoline area holds the 64-bit trampoline code we'll run, the
  // GDT with the 64-bit code segment we'll run it in, and the long jump
  // descriptor we'll use to get there.
  *trampoline = (struct trampoline){
      .code = TRAMPOLINE_CODE,
      .gdt = GDT_ENTRIES,
      .ljmp =
          {
              .eip = trampoline->code,
              .cs = 1 << 3,
          },
  };

  // Tell the compiler all of the trampoline area is read.
  // Otherwise it might conclude that only gdt and ljmp are used.
  __asm__ volatile("" ::"m"(*trampoline));

  __asm__ volatile(
      // Load the GDT stored safely in the trampoline area.  We can
      // access the descriptor via the stack segment and stack pointer
      // using the Multiboot-provided flat segments.  Hereafter we can
      // use only the registers and the already-running code and data
      // segments, since there are no 32-bit segments in the new GDT.
      "lgdt %[lgdt]\n\t"
      // Jump into the 64-bit trampoline code.  The jump descriptor
      // resides in the trampoline area, so the compiler will access it
      // through a non-stack register here.
      "ljmp *%[ljmp]\n\t" ::[lgdt] "m"(lgdt),
      [ ljmp ] "m"(trampoline->ljmp),
      // The 64-bit trampoline code copies the kernel into place and
      // then jumps to its entry point, as instructed here:
      "D"(copy_dest),      // %rdi: destination pointer
      "S"(copy_src),       // %rsi: source pointer
      "c"(copy_size / 8),  // %rcx: count of 8-byte words
      "a"(kernel_entry),   // %rax: kernel entry point
      "b"(zbi)             // %rbx: ZBI data pointer for kernel
  );
  __builtin_unreachable();
}
