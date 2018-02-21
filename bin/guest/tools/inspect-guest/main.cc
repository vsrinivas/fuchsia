// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/function.h>
#include <iostream>

#include "garnet/bin/guest/tools/inspect-guest/dump.h"
#include "garnet/bin/guest/tools/inspect-guest/serial.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/files/directory.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/fxl/strings/string_printf.h"

std::string svc_prefix;
using CommandFunc = fbl::Function<void()>;

static bool usage() {
  std::cerr << "Usage: inspect-guest <package> <command>\n"
            << "\n"
            << "Commands:\n"
            << "  dump <hex-addr> <hex-len>\n"
            << "  serial\n";
  return false;
}

static bool parse_args(int argc, const char** argv, CommandFunc* cmd_func) {
  fxl::StringView cmd_view(argv[2]);
  if (cmd_view == "dump" && argc == 5) {
    fxl::StringView addr_view(argv[3]);
    zx_vaddr_t addr;
    if (!fxl::StringToNumberWithError(addr_view, &addr, fxl::Base::k16)) {
      std::cerr << "Invalid address " << addr_view << "\n";
      return usage();
    }
    fxl::StringView len_view(argv[4]);
    size_t len;
    if (!fxl::StringToNumberWithError(len_view, &len, fxl::Base::k16)) {
      std::cerr << "Invalid length " << len_view << "\n";
      return usage();
    }
    *cmd_func = [addr, len]() { handle_dump(addr, len); };
  } else if (cmd_view == "serial" && argc == 3) {
    *cmd_func = handle_serial;
  } else {
    return usage();
  }
  return true;
}

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  CommandFunc cmd_func;
  if (!parse_args(argc, argv, &cmd_func)) {
    return ZX_ERR_INVALID_ARGS;
  }

  const char* pkg = argv[1];
  svc_prefix = fxl::StringPrintf("/root_info_experimental/sys/%s/export/", pkg);
  if (!files::IsDirectory(svc_prefix)) {
    std::cerr << "Package " << pkg << " is not running\n";
    return ZX_ERR_IO_NOT_PRESENT;
  }

  cmd_func();
  loop.Run();
  return 0;
}
