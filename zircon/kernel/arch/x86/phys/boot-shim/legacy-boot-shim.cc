// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/arch/x86/extension.h>
#include <lib/arch/x86/system.h>
#include <lib/boot-options/word-view.h>
#include <lib/zbitl/error_stdio.h>
#include <stdio.h>
#include <stdlib.h>

#include <hwreg/x86msr.h>
#include <ktl/string_view.h>
#include <phys/allocation.h>
#include <phys/arch.h>
#include <phys/main.h>
#include <phys/symbolize.h>

#include "../legacy-boot.h"
#include "stdout.h"
#include "trampoline-boot.h"

namespace {

// These synthetic command-line arguments are always injected between the
// incoming ZBI's items and the legacy boot loader's actual command line.
constexpr ktl::string_view kBootLoaderNamePrefix = "bootloader.name=";
constexpr ktl::string_view kBootLoaderInfoPrefix = " bootloader.info=";
constexpr ktl::string_view kBootLoaderBuildIdPrefix = " bootloader.build-id=";

// If "bootloader.zbi.serial-number=foo" appears in a command line item in the
// ZBI, then we'll synthesize a ZBI_TYPE_SERIAL_NUMBER item containing "foo".
constexpr ktl::string_view kSerialNumberEq = "bootloader.zbi.serial-number=";

// This runs in a first pass that counts the size and has to run before any
// memory allocation can be done, then a second pass that actually copies.
constexpr auto AssembleCmdline = [](auto&& add) {
  add(kBootLoaderNamePrefix);
  add(Symbolize::kProgramName_);

  if (!gLegacyBoot.bootloader.empty()) {
    add(kBootLoaderInfoPrefix);
    add(gLegacyBoot.bootloader);
  }

  add(kBootLoaderBuildIdPrefix);
  add(Symbolize::GetInstance()->BuildIdString());
  add(" ");

  add(gLegacyBoot.cmdline);

  // The ZBI protocol specification technically requires a NUL-terminated
  // payload, though that's really an obsolete requirement nothing assumes.
  add({"", 1});
};

uint32_t CmdlinePayloadSize() {
  size_t size = 0;
  AssembleCmdline([&size](ktl::string_view str) { size += str.size(); });
  return ZBI_ALIGN(size);
}

void FillCmdlinePayload(ktl::span<ktl::byte> payload) {
  AssembleCmdline([payload](ktl::string_view str) mutable {
    auto data = reinterpret_cast<char*>(payload.data());
    payload = payload.subspan(str.copy(data, payload.size()));
  });
}

uint32_t MemConfigPayloadSize() { return gLegacyBoot.mem_config.size_bytes(); }

void FillMemconfigPayload(ktl::span<ktl::byte> payload) {
  auto bytes = cpp20::as_bytes(gLegacyBoot.mem_config);
  memcpy(payload.data(), bytes.data(), bytes.size());
}

zbitl::ByteView SerialNumberFromCmdline(BootZbi::InputZbi zbi) {
  zbitl::ByteView result;
  for (auto [header, payload] : zbi) {
    if (header->type == ZBI_TYPE_CMDLINE) {
      ktl::string_view line{
          reinterpret_cast<const char*>(payload.data()),
          payload.size(),
      };
      for (ktl::string_view word : WordView(line)) {
        if (word.starts_with(kSerialNumberEq)) {
          word.remove_prefix(kSerialNumberEq.size());
          result = {
              reinterpret_cast<const ktl::byte*>(word.data()),
              word.size(),
          };
        }
      }
    }
  }
  zbi.ignore_error();
  return result;
}

}  // namespace

void PhysMain(void* ptr, arch::EarlyTicks boot_ticks) {
  StdoutInit();

  ApplyRelocations();

  // This also fills in gLegacyBoot.
  InitMemory(ptr);

  StdoutFromCmdline(gLegacyBoot.cmdline);

  if (!gLegacyBoot.bootloader.empty()) {
    printf("%s: legacy boot loader: %.*s\n", Symbolize::kProgramName_,
           static_cast<int>(gLegacyBoot.bootloader.size()), gLegacyBoot.bootloader.data());
  }

  // Remove any incoming trailing NULs, just in case.
  if (auto pos = gLegacyBoot.cmdline.find_last_not_of('\0'); pos != ktl::string_view::npos) {
    gLegacyBoot.cmdline.remove_suffix(gLegacyBoot.cmdline.size() - (pos + 1));
  } else {
    gLegacyBoot.cmdline = {};
  }

  if (gLegacyBoot.cmdline.empty()) {
    printf("%s: No kernel command from legacy boot loader.\n", Symbolize::kProgramName_);
  } else {
    printf("%s: Kernel command line: %.*s\n", Symbolize::kProgramName_,
           static_cast<int>(gLegacyBoot.cmdline.size()), gLegacyBoot.cmdline.data());
  }

  if (gLegacyBoot.ramdisk.empty()) {
    printf("%s: Missing or empty RAMDISK: No ZBI!\n", Symbolize::kProgramName_);
    abort();
  } else {
    printf("%s: ZBI @ [%p, %p) from RAMDISK\n", Symbolize::kProgramName_,
           gLegacyBoot.ramdisk.data(), gLegacyBoot.ramdisk.data() + gLegacyBoot.ramdisk.size());
  }

  BootZbi::InputZbi zbi(cpp20::as_bytes(gLegacyBoot.ramdisk));

  TrampolineBoot boot;
  if (auto result = boot.Init(zbi); result.is_error()) {
    printf("%s: Not a bootable ZBI: ", Symbolize::kProgramName_);
    zbitl::PrintViewCopyError(result.error_value());
    abort();
  }

  // Scan the ZBI-embedded command line switches for one meant specifically to
  // tell the shim to synthesize a ZBI_TYPE_SERIAL_NUMBER item.
  zbitl::ByteView serial_number = SerialNumberFromCmdline(zbi);

  // Precalculate the space needed for extra "boot loader" ZBI items
  // synthesized from the legacy boot loader information.
  uint32_t extra = 0;

  // We don't pack the uart ZBI item though we could, because we only got
  // that setting from the command line anyway so the kernel can just use
  // the command line as well.

  // Start with memory info.
  const uint32_t memconfig_size = MemConfigPayloadSize();
  extra += sizeof(zbi_header_t) + memconfig_size;

  const uint32_t cmdline_size = CmdlinePayloadSize();
  if (cmdline_size != 0) {
    extra += sizeof(zbi_header_t) + cmdline_size;
  }

  if (!serial_number.empty()) {
    extra += sizeof(zbi_header_t) + ZBI_ALIGN(serial_number.size_bytes());
  }

  if (auto result = boot.Load(extra); result.is_error()) {
    printf("%s: Failed to load ZBI: ", Symbolize::kProgramName_);
    zbitl::PrintViewCopyError(result.error_value());
    abort();
  }

  // Fill in the extra data items.

  if (auto result = boot.DataZbi().Append({
          .type = ZBI_TYPE_MEM_CONFIG,
          .length = memconfig_size,
      });
      result.is_ok()) {
    FillMemconfigPayload((*result.value()).payload);
  } else {
    printf("%s: Failed to append %" PRIu32 " bytes of MEM_CONFIG data to ZBI: ",
           Symbolize::kProgramName_, memconfig_size);
    zbitl::PrintViewError(result.error_value());
    abort();
  }

  if (cmdline_size != 0) {
    if (auto result = boot.DataZbi().Append({
            .type = ZBI_TYPE_CMDLINE,
            .length = cmdline_size,
        });
        result.is_ok()) {
      FillCmdlinePayload((*result.value()).payload);
    } else {
      printf("%s: Failed to append %" PRIu32 " bytes of CMDLINE data to ZBI: ",
             Symbolize::kProgramName_, cmdline_size);
      zbitl::PrintViewError(result.error_value());
      abort();
    }
  }

  if (!serial_number.empty()) {
    auto result = boot.DataZbi().Append({.type = ZBI_TYPE_SERIAL_NUMBER}, serial_number);
    if (result.is_error()) {
      printf("%s: Failed to append %zu bytes of SERIAL_NUMBER data to ZBI: ",
             Symbolize::kProgramName_, serial_number.size_bytes());
      zbitl::PrintViewError(result.error_value());
      abort();
    }
  }

  EnablePaging();

  boot.Boot();
}
