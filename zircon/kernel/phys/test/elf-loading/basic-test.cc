// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/elfldltl/diagnostics.h>
#include <lib/elfldltl/dynamic.h>
#include <lib/elfldltl/layout.h>
#include <lib/elfldltl/link.h>
#include <lib/elfldltl/load.h>
#include <lib/elfldltl/memory.h>
#include <lib/elfldltl/phdr.h>
#include <lib/memalloc/range.h>
#include <lib/zbitl/error-stdio.h>
#include <lib/zbitl/items/bootfs.h>
#include <lib/zbitl/view.h>
#include <stdio.h>
#include <zircon/limits.h>

#include <fbl/alloc_checker.h>
#include <ktl/atomic.h>
#include <ktl/byte.h>
#include <ktl/limits.h>
#include <ktl/optional.h>
#include <ktl/span.h>
#include <ktl/string_view.h>
#include <phys/kernel-package.h>
#include <phys/symbolize.h>
#include <phys/zbitl-allocation.h>

#include "../test-main.h"

#include <ktl/enforce.h>

namespace {

using BootfsView = zbitl::BootfsView<ktl::span<const ktl::byte>>;

using Elf = elfldltl::Elf<elfldltl::ElfClass::kNative>;
using Dyn = typename Elf::Dyn;
using Phdr = typename Elf::Phdr;

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

  BootfsView bootfs;
  if (auto result = kernelfs.GetBootfs(kNamespace); result.is_error()) {
    zbitl::PrintBootfsError(result.error_value());
    return 1;
  } else {
    bootfs = ktl::move(result).value();
  }
  auto it = bootfs.find(kGetInt);
  if (auto result = bootfs.take_error(); result.is_error()) {
    zbitl::PrintBootfsError(result.error_value());
    return 1;
  }
  if (it == bootfs.end()) {
    printf("FAILED: Cannot find %.*s/%.*s in BOOTFS\n",             //
           static_cast<int>(kNamespace.size()), kNamespace.data(),  //
           static_cast<int>(kGetInt.size()), kGetInt.data());
    return 1;
  }

  // Now that we've found the module, we can load it.
  auto diag = elfldltl::PanicDiagnostics("FAILED: ");

  ktl::span<ktl::byte> elf_bytes = {const_cast<ktl::byte*>(it->data.data()), it->data.size()};

  // We are just reading from the file and so don't worry about the base address.
  elfldltl::DirectMemory file{elf_bytes};

  // Decode the basic ELF headers.
  auto phdr_allocator = elfldltl::NoArrayFromFile<Phdr>();
  auto [ehdr_owner, phdrs_owner] = *elfldltl::LoadHeadersFromFile<Elf>(diag, file, phdr_allocator);
  const Elf::Ehdr& ehdr = ehdr_owner;
  ktl::span<const Phdr> phdrs = phdrs_owner;

  // Since `bootfs`'s underlying buffer is ZBI_BOOTFS_PAGE_SIZE-aligned (a
  // KernelStorage guarantee), so too will the ELF payload (a BOOTFS
  // guarantee). Assert that this implies runtime page size alignment as well.
  //
  // TODO(mcgrathr): Using `ZX_PAGE_SIZE` here as the runtime page size for ELF
  // purposes in physboot is the right thing to do now, but should be
  // revisited.
  static_assert(ZX_PAGE_SIZE <= ZBI_BOOTFS_PAGE_SIZE);

  // Parse phdrs to find the dynamic sections and to validate that the load
  // segments comprise a contiguous layout. A contiguous layout - paired with
  // the fact that the file is already appropriately aligned - implies that
  // the file in memory is already suitable as a load image.
  ktl::optional<Phdr> dyn_phdr;
  uint64_t vaddr_start, vaddr_size;
  elfldltl::DecodePhdrs(diag, phdrs, elfldltl::PhdrDynamicObserver<Elf>(dyn_phdr),
                        elfldltl::PhdrLoadObserver<Elf, elfldltl::PhdrLoadPolicy::kContiguous>(
                            ZX_PAGE_SIZE, vaddr_start, vaddr_size));
  file.set_base(vaddr_start);

  if (!dyn_phdr) {
    printf("FAILED: no dynamic sections found\n");
    return 1;
  }

  auto dyn_allocator = elfldltl::NoArrayFromFile<Dyn>();
  ktl::span<const Dyn> dyn;
  if (auto result = file.ReadArrayFromFile<Dyn>(dyn_phdr->offset(), dyn_allocator,
                                                dyn_phdr->filesz() / sizeof(Dyn));
      !result) {
    printf("FAILED: to read dynamic sections\n");
    return 1;
  } else {
    dyn = *result;
  }

  // Parse the dynamic sections for relocation info.
  elfldltl::RelocationInfo<Elf> reloc_info;
  elfldltl::DecodeDynamic(diag, file, dyn, elfldltl::DynamicRelocationInfoObserver(reloc_info));

  // Apply relocations.
  auto runtime_load_addr = reinterpret_cast<uint64_t>(elf_bytes.data());
  uint64_t load_bias = runtime_load_addr - vaddr_start;
  if (!elfldltl::RelocateRelative(file, reloc_info, load_bias)) {
    printf("FAILED: relocation failed\n");
    return 1;
  }
  ktl::atomic_signal_fence(ktl::memory_order_seq_cst);

  // We should now be able to access GetInt()!
  auto GetInt = reinterpret_cast<int (*)()>(ehdr.entry() + load_bias);
  constexpr int kExpected = 42;
  if (int actual = GetInt(); actual != kExpected) {
    printf("FAILED: Expected %d; got %d\n", kExpected, actual);
    return 1;
  }

  return 0;
}
