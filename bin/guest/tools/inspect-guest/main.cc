// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fdio/util.h>
#include <pretty/hexdump.h>
#include <iostream>

#include "garnet/lib/machina/bits.h"
#include "garnet/lib/machina/fidl/inspect.fidl.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/fxl/strings/string_printf.h"

static constexpr const char* kServiceName = machina::InspectService::Name_;
using InspectFunc = fbl::Function<void(zx::vmo)>;

void usage() {
  std::cerr << "Usage: inspect-guest <package> <command>\n"
            << "\n"
            << "Commands:\n"
            << "  dump <hex-addr> <hex-len>\n";
}

void dump(zx::vmo vmo, fxl::StringView addr_view, fxl::StringView len_view) {
  zx_vaddr_t addr;
  if (!fxl::StringToNumberWithError(addr_view, &addr, fxl::Base::k16)) {
    std::cerr << "Invalid address " << addr_view << "\n";
    return usage();
  }
  size_t len;
  if (!fxl::StringToNumberWithError(len_view, &len, fxl::Base::k16)) {
    std::cerr << "Invalid length " << len_view << "\n";
    return usage();
  }

  uint64_t vmo_size;
  zx_status_t status = vmo.get_size(&vmo_size);
  if (status != ZX_OK) {
    std::cerr << "Failed to get guest memory size\n";
    return;
  } else if (addr > vmo_size || addr > vmo_size - len) {
    std::cerr << "Range exceeds guest memory\n";
    return;
  }
  uintptr_t guest_addr;
  status =
      zx::vmar::root_self().map(0 /* vmar_offset */, vmo, 0 /* vmo_offset */,
                                vmo_size, ZX_VM_FLAG_PERM_READ, &guest_addr);
  if (status != ZX_OK) {
    std::cerr << "Failed to map guest memory\n";
    return;
  }

  std::cout << std::hex << "Dumping [0x" << addr << ", 0x" << addr + len
            << "] of 0x" << vmo_size << ":\n";
  hexdump_ex(reinterpret_cast<void*>(guest_addr + addr), len, addr);
}

bool parse_args(int argc, const char** argv, InspectFunc* func) {
  if (argc < 3) {
    usage();
    return false;
  }
  const char* command = argv[2];
  if (!strcmp(command, "dump") && argc == 5) {
    fxl::StringView addr_view(argv[3]);
    fxl::StringView len_view(argv[4]);
    *func = [addr_view, len_view](zx::vmo vmo) {
      dump(fbl::move(vmo), addr_view, len_view);
    };
    return true;
  }
  usage();
  return false;
}

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;

  InspectFunc func;
  if (!parse_args(argc, argv, &func)) {
    return ZX_ERR_INVALID_ARGS;
  }

  const char* package = argv[1];
  std::string path = fxl::StringPrintf(
      "/root_info_experimental/sys/%s/export/%s", package, kServiceName);
  if (!files::IsFile(path)) {
    std::cerr << "Package " << package << " is not running\n";
    return ZX_ERR_IO_NOT_PRESENT;
  }

  fidl::InterfacePtr<machina::InspectService> inspect;
  zx_status_t status = fdio_service_connect(
      path.c_str(), inspect.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    std::cerr << "Failed to connect to " << kServiceName << "\n";
    return ZX_ERR_UNAVAILABLE;
  }

  inspect->FetchGuestMemory([&loop, &func](zx::vmo vmo) {
    func(fbl::move(vmo));
    return loop.PostQuitTask();
  });

  loop.Run();
  return 0;
}
