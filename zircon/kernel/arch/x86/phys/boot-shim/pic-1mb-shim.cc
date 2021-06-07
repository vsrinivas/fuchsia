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

  if (auto result = boot.Load(); result.is_error()) {
    printf("pic-1mb-boot-shim: Failed to load ZBI: ");
    zbitl::PrintViewCopyError(result.error_value());
    abort();
  }

  boot.Boot();
}
