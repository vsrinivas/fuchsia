// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/cli/dump.h"
#include "garnet/bin/guest/cli/launch.h"
#include "garnet/bin/guest/cli/serial.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/fxl/strings/string_printf.h"

machina::InspectServicePtr inspect_svc;
std::string svc_path;

using CommandFunc = fbl::Function<void()>;

static void usage() {
  std::cerr << "Usage: guest <command> <package> <command-args>...\n"
            << "\n"
            << "Commands:\n"
            << "  dump   <package> <hex-addr> <hex-len>\n"
            << "  launch <package> <package-args>...\n"
            << "  serial <package>\n";
}

static bool parse_args(int argc, const char** argv, CommandFunc* func) {
  if (argc < 3) {
    return false;
  }
  fxl::StringView cmd_view(argv[1]);
  if (cmd_view == "dump" && argc == 5) {
    fxl::StringView addr_view(argv[3]);
    zx_vaddr_t addr;
    if (!fxl::StringToNumberWithError(addr_view, &addr, fxl::Base::k16)) {
      std::cerr << "Invalid address " << addr_view << "\n";
      return false;
    }
    fxl::StringView len_view(argv[4]);
    size_t len;
    if (!fxl::StringToNumberWithError(len_view, &len, fxl::Base::k16)) {
      std::cerr << "Invalid length " << len_view << "\n";
      return false;
    }
    *func = [addr, len] { handle_dump(addr, len); };
  } else if (cmd_view == "launch" && argc >= 3) {
    *func = [argc, argv] { handle_launch(argc - 2, argv + 2); };
  } else if (cmd_view == "serial" && argc == 3) {
    *func = [] { handle_serial(connect); };
  } else {
    return false;
  }
  svc_path = fxl::StringPrintf("/root_info_experimental/sys/%s/export/%s",
                               argv[2], machina::InspectService::Name_);
  return true;
}

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  CommandFunc func;
  if (!parse_args(argc, argv, &func)) {
    usage();
    return ZX_ERR_INVALID_ARGS;
  }

  func();
  loop.Run();
  return 0;
}
