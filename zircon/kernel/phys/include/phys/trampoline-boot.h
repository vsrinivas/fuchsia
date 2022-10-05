// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_TRAMPOLINE_BOOT_H_
#define ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_TRAMPOLINE_BOOT_H_

#include <zircon/assert.h>

#include <ktl/optional.h>
#include <phys/boot-zbi.h>

class TrampolineBoot : public BootZbi {
 public:
  // Legacy x86 ZBI provide absolute offset, while newer ones use a relative offset.
  static constexpr uint64_t kLegacyLoadAddress = 1 << 20;

  using BootZbi::Error;

  // Inits a default constructed object. Just like |BootZbi::*| but performs additional
  // initialization depending on the zbi format. (Fixed or position independent entry address).
  fit::result<Error> Init(InputZbi zbi);
  fit::result<Error> Init(InputZbi zbi, InputZbi::iterator kernel_item);

  uint64_t KernelEntryAddress() const { return kernel_entry_address_; }

  bool MustRelocateDataZbi() const {
    return kernel_load_address_ && FixedKernelOverlapsData(kernel_load_address_.value());
  }

  fit::result<Error> Load(uint32_t extra_data_capacity = 0,
                          ktl::optional<uint64_t> kernel_load_address = ktl::nullopt,
                          ktl::optional<uint64_t> data_load_address = ktl::nullopt);

  [[noreturn]] void Boot(ktl::optional<void*> argument = {});

  void Log();

 private:
  class Trampoline;

  void set_kernel_load_address(uint64_t load_address) {
    kernel_load_address_ = load_address;
    kernel_entry_address_ = load_address + KernelHeader()->entry;
  }

  void LogFixedAddresses() const;

  // Must be called after BootZbi::Init and before Load.
  void SetKernelAddresses();

  ktl::optional<uint64_t> kernel_load_address_;
  ktl::optional<uint64_t> data_load_address_;
  uint64_t kernel_entry_address_ = 0;
  Trampoline* trampoline_ = nullptr;
};

#endif  // ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_TRAMPOLINE_BOOT_H_
