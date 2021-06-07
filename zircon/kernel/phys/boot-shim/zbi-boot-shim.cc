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

void ZbiMain(void* zbi, arch::EarlyTicks boot_ticks) {
  InitMemory(zbi);

  BootZbi::InputZbi input_zbi_view(
      zbitl::StorageFromRawHeader(static_cast<const zbi_header_t*>(zbi)));

  BootZbi boot;
  if (auto result = boot.Init(input_zbi_view); result.is_error()) {
    printf("boot-shim: Not a bootable ZBI: ");
    zbitl::PrintViewCopyError(result.error_value());
    abort();
  }

  if (auto result = boot.Load(); result.is_error()) {
    printf("boot-shim: Failed to load ZBI: ");
    zbitl::PrintViewCopyError(result.error_value());
    abort();
  }

  boot.Boot();
}
