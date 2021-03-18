// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/zbitl/error_stdio.h>
#include <stdio.h>
#include <stdlib.h>

#include <phys/allocation.h>
#include <phys/boot-zbi.h>
#include <phys/main.h>
#include <phys/symbolize.h>

// This is a trivial "no-op" ZBI-to-ZBI boot shim.  It simply treats the data
// ZBI as a whole bootable ZBI and boots it using the modern ZBI booting
// protocol, which is always position-independent and fairly uniform across
// machines.  That means the original combined boot image contains two kernel
// items: this boot shim and then the actual kernel.

const char Symbolize::kProgramName_[] = "zbi-boot-shim";

// On x86, this can be linked at the old fixed 1MB address to make it into a
// compatibility shim that is itself loaded using by the legacy 1MB loading
// protocol with an old-style fixed entry point address.  The kernel it loads
// must be in the new uniform format.

// TODO(fxbug.dev/68762): x86 needs page table setup

void ZbiMain(void* ptr, arch::EarlyTicks boot_ticks) {
  ApplyRelocations();
  InitMemory(ptr);

  auto zbi_ptr = static_cast<const zbi_header_t*>(ptr);
  BootZbi::InputZbi zbi(zbitl::StorageFromRawHeader(zbi_ptr));

  BootZbi boot;
  if (auto result = boot.Init(zbi); result.is_error()) {
    printf("boot-shim: Not a bootable ZBI: ");
    zbitl::PrintViewCopyError(result.error_value());
    abort();
  }

  if (auto result = boot.Load(); result.is_error()) {
    printf("boot-shim: Failed to load ZBI: ");
    zbitl::PrintViewCopyError(result.error_value());
    abort();
  }

#define ADDR "0x%016" PRIx64
  printf("boot-shim: ZBI kernel @ [" ADDR ", " ADDR ")\n", boot.KernelLoadAddress(),
         boot.KernelLoadAddress() + boot.KernelLoadSize());
  printf("boot-shim: ZBI data   @ [" ADDR ", " ADDR ")\n", boot.DataLoadAddress(),
         boot.DataLoadAddress() + boot.DataLoadSize());
  printf("boot-shim: Booting ZBI kernel at entry point " ADDR "...\n", boot.KernelEntryAddress());
  boot.Boot();
}
