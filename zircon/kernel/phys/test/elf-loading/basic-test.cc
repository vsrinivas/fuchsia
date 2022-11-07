// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/zbitl/error-stdio.h>
#include <lib/zbitl/items/bootfs.h>
#include <lib/zbitl/view.h>
#include <stdio.h>

#include <ktl/byte.h>
#include <ktl/span.h>
#include <ktl/string_view.h>
#include <phys/elf-image.h>
#include <phys/kernel-package.h>
#include <phys/symbolize.h>
#include <phys/zbitl-allocation.h>

#include "../test-main.h"
#include "get-int.h"

#include <ktl/enforce.h>

namespace {

// The name of ELF module to be loaded.
constexpr ktl::string_view kGetInt = "get-int";

// The BOOTFS namespace under which kGetInt lives.
constexpr ktl::string_view kNamespace = "loadables";

}  // namespace

int TestMain(void* zbi_ptr, arch::EarlyTicks) {
  MainSymbolize symbolize("basic-elf-loading-test");

  // Initialize memory for allocation/free.
  InitMemory(zbi_ptr);

  zbitl::View zbi(
      zbitl::StorageFromRawHeader<ktl::span<ktl::byte>>(static_cast<zbi_header_t*>(zbi_ptr)));
  KernelStorage kernelfs;
  kernelfs.Init(zbi);

  KernelStorage::Bootfs bootfs;
  if (auto result = kernelfs.root().subdir(kNamespace); result.is_error()) {
    zbitl::PrintBootfsError(result.error_value());
    return 1;
  } else {
    bootfs = ktl::move(result).value();
  }

  printf("Loading %.*s...\n", static_cast<int>(kGetInt.size()), kGetInt.data());
  ElfImage elf;
  if (auto result = elf.Init(bootfs, kGetInt, true); result.is_error()) {
    zbitl::PrintBootfsError(result.error_value());
    return 1;
  }

  ZX_ASSERT(!elf.has_patches());

  elf.Load();
  elf.Relocate();

  printf("Calling entry point...\n");

  // We should now be able to access GetInt()!
  constexpr int kExpected = 42;
  if (int actual = elf.Call<decltype(GetInt)>(); actual != kExpected) {
    printf("FAILED: Expected %d; got %d\n", kExpected, actual);
    return 1;
  }

  return 0;
}
