// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/memalloc/allocator.h>
#include <lib/zbitl/error_stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/assert.h>

#include <phys/allocation.h>
#include <phys/main.h>
#include <phys/symbolize.h>

#include "trampoline-boot.h"

const char Symbolize::kProgramName_[] = "pic-1mb-boot-shim";

void ZbiMain(void* ptr, arch::EarlyTicks boot_ticks) {
  InitMemory(ptr);

  auto zbi_ptr = static_cast<const zbi_header_t*>(ptr);
  BootZbi::InputZbi zbi(zbitl::StorageFromRawHeader(zbi_ptr));

  TrampolineBoot boot;
  if (auto result = boot.Init(zbi); result.is_error()) {
    printf("pic-1mb-boot-shim: Not a bootable ZBI: ");
    zbitl::PrintViewCopyError(result.error_value());
    abort();
  }

  // Now we know how much space the kernel image needs.
  // Reserve it at the fixed load address.
  auto& alloc = Allocation::GetAllocator();
  ZX_ASSERT(alloc.RemoveRange(TrampolineBoot::kFixedLoadAddress, boot.KernelLoadSize()).is_ok());

  if (auto result = boot.Load(); result.is_error()) {
    printf("pic-1mb-boot-shim: Failed to load ZBI: ");
    zbitl::PrintViewCopyError(result.error_value());
    abort();
  }

#define ADDR "0x%016" PRIx64
  printf("pic-1mb-boot-shim: ZBI kernel @ [" ADDR ", " ADDR ")\n", boot.KernelLoadAddress(),
         boot.KernelLoadAddress() + boot.KernelLoadSize());
  printf("pic-1mb-boot-shim: ZBI data   @ [" ADDR ", " ADDR ")\n", boot.DataLoadAddress(),
         boot.DataLoadAddress() + boot.DataLoadSize());
  printf("pic-1mb-boot-shim: Relocated  @ [" ADDR ", " ADDR ")\n",
         TrampolineBoot::kFixedLoadAddress,
         TrampolineBoot::kFixedLoadAddress + boot.KernelLoadSize());
  printf("pic-1mb-boot-shim: Booting ZBI kernel at entry point " ADDR "...\n",
         boot.KernelEntryAddress());
  boot.Boot();
}
