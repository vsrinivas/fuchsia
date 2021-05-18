// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stddef.h>

#include <hwreg/asm.h>

#include "linuxboot.h"

int main(int argc, char** argv) {
  return hwreg::AsmHeader()
      .Macro("SETUP_HEADER_SETUP_SECTS", offsetof(linuxboot::setup_header, setup_sects))
      .Macro("SETUP_HEADER_SYSSIZE", offsetof(linuxboot::setup_header, syssize))
      .Macro("SETUP_HEADER_BOOT_FLAG", offsetof(linuxboot::setup_header, boot_flag))
      .Macro("LINUXBOOT_BOOT_FLAG", linuxboot::setup_header::kBootFlag)
      .Macro("SETUP_HEADER_JUMP", offsetof(linuxboot::setup_header, jump))
      .Macro("SETUP_HEADER_HEADER", offsetof(linuxboot::setup_header, header))
      .Macro("SETUP_HEADER_VERSION", offsetof(linuxboot::setup_header, version))
      .Macro("SETUP_HEADER_LOADFLAGS", offsetof(linuxboot::setup_header, loadflags))
      .Macro("LOADFLAGS_LOADED_HIGH", linuxboot::setup_header::LoadFlags::kLoadedHigh)
      .Macro("SETUP_HEADER_INITRD_ADDR_MAX", offsetof(linuxboot::setup_header, initrd_addr_max))
      .Macro("SETUP_HEADER_KERNEL_ALIGNMENT", offsetof(linuxboot::setup_header, kernel_alignment))
      .Macro("SETUP_HEADER_CMDLINE_SIZE", offsetof(linuxboot::setup_header, cmdline_size))
      .Macro("SIZEOF_SETUP_HEADER", sizeof(linuxboot::setup_header))
      .Macro("BOOT_PARAMS_HDR", offsetof(linuxboot::boot_params, hdr))
      .Macro("SIZEOF_BOOT_PARAMS", sizeof(linuxboot::boot_params))
      .Main(argc, argv);
}
