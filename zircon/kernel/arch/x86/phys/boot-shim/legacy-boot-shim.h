// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_PHYS_BOOT_SHIM_LEGACY_BOOT_SHIM_H_
#define ZIRCON_KERNEL_ARCH_X86_PHYS_BOOT_SHIM_LEGACY_BOOT_SHIM_H_

#include <lib/acpi_lite.h>
#include <lib/boot-shim/acpi.h>
#include <lib/boot-shim/boot-shim.h>
#include <lib/boot-shim/pool-mem-config.h>
#include <lib/boot-shim/test-serial-number.h>
#include <stdio.h>

#include "../legacy-boot.h"

class TrampolineBoot;

using LegacyBootShimBase = boot_shim::BootShim<  //
    boot_shim::PoolMemConfigItem,                //
    boot_shim::AcpiUartItem,                     //
    boot_shim::AcpiRsdpItem,                     //
    boot_shim::TestSerialNumberItem>;

class LegacyBootShim : public LegacyBootShimBase {
 public:
  LegacyBootShim(const char* name, const LegacyBoot& info, FILE* log = stdout)
      : LegacyBootShimBase(name, log), input_zbi_(cpp20::as_bytes(info.ramdisk)) {
    set_info(info.bootloader);
    set_cmdline(info.cmdline);
    Log(input_zbi_.storage());
    Check("Error scanning ZBI", Get<SerialNumber>().Init(input_zbi_));
  }

  void InitMemConfig(const memalloc::Pool& pool) { Get<boot_shim::PoolMemConfigItem>().Init(pool); }

  void InitAcpi(const acpi_lite::AcpiParser& parser) {
    Get<boot_shim::AcpiUartItem>().Init(parser);
    Get<boot_shim::AcpiRsdpItem>().Init(parser);
  }

  InputZbi& input_zbi() { return input_zbi_; }

  bool Load(TrampolineBoot& boot);

 private:
  using SerialNumber = boot_shim::TestSerialNumberItem;

  bool StandardLoad(TrampolineBoot& boot);

  // This gets first crack before StandardLoad.
  // If it returns false then StandardLoad  is done.
  bool BootQuirksLoad(TrampolineBoot& boot);

  // Helper for BootQuirksLoad to recognize an apparently valid bootable ZBI
  // (or a simply empty one, which can get the standard error path).
  bool IsProperZbi() const;

  InputZbi input_zbi_;
};

#endif  // ZIRCON_KERNEL_ARCH_X86_PHYS_BOOT_SHIM_LEGACY_BOOT_SHIM_H_
