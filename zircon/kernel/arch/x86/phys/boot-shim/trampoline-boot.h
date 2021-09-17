// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_PHYS_BOOT_SHIM_TRAMPOLINE_BOOT_H_
#define ZIRCON_KERNEL_ARCH_X86_PHYS_BOOT_SHIM_TRAMPOLINE_BOOT_H_

#include <ktl/optional.h>
#include <phys/boot-zbi.h>

class TrampolineBoot : public BootZbi {
 public:
  // This is the physical address that the legacy ZBI kernel must be loaded at.
  static constexpr uint64_t kFixedLoadAddress = 1 << 20;

  using BootZbi::BootZbi;

  // In the legacy fixed-address format, the entry address is always above 1M.
  // In the new format, it's an offset and in practice it's never > 1M.  So
  // this is a safe-enough heuristic to distinguish the new from the old.
  bool Relocating() const { return KernelHeader()->entry > kFixedLoadAddress; }

  uint64_t KernelEntryAddress() const {
    return Relocating() ? KernelHeader()->entry : BootZbi::KernelEntryAddress();
  }

  bool MustRelocateDataZbi() const {
    return Relocating() && FixedKernelOverlapsData(kFixedLoadAddress);
  }

  fitx::result<Error> Load(uint32_t extra_data_capacity = 0);

  [[noreturn]] void Boot(ktl::optional<void*> argument = {});

 private:
  class Trampoline;

  void LogFixedAddresses() const;

  Trampoline* trampoline_ = nullptr;
};

#endif  // ZIRCON_KERNEL_ARCH_X86_PHYS_BOOT_SHIM_TRAMPOLINE_BOOT_H_
