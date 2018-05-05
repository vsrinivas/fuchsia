// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/function.h>

#include <iostream>

#include "garnet/bin/guest/cli/dump.h"
#include "garnet/bin/guest/cli/launch.h"
#include "garnet/bin/guest/cli/list.h"
#include "garnet/bin/guest/cli/serial.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/fxl/strings/string_printf.h"

using CommandFunc = fbl::Function<void()>;

static void usage() {
  std::cerr << "Usage: guest <command> <package> <command-args>...\n"
            << "\n"
            << "Commands:\n"
            << "  launch <package> <vmm-args>...\n"
            << "  list\n"
            << "  serial <guest_id>\n"
            << "  dump   <guest_id> <hex-addr> <hex-len>\n";
}

bool parse_guest_id(const char* arg, uint32_t* guest_id) {
  fxl::StringView guest_id_view(arg);
  if (!fxl::StringToNumberWithError(guest_id_view, guest_id)) {
    std::cerr << "Invalid guest id " << guest_id << "\n";
    return false;
  }
  return true;
}

static bool parse_args(int argc, const char** argv, CommandFunc* func) {
  if (argc < 2) {
    return false;
  }
  fxl::StringView cmd_view(argv[1]);
  if (cmd_view == "dump" && argc == 5) {
    uint32_t guest_id;
    if (!parse_guest_id(argv[2], &guest_id)) {
      return false;
    }
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
    *func = [guest_id, addr, len] { handle_dump(guest_id, addr, len); };
  } else if (cmd_view == "launch" && argc >= 3) {
    *func = [argc, argv]() { handle_launch(argc - 2, argv + 2); };
  } else if (cmd_view == "serial" && argc == 3) {
    uint32_t guest_id;
    if (!parse_guest_id(argv[2], &guest_id)) {
      return false;
    }
    *func = [guest_id]() { handle_serial(guest_id); };
  } else if (cmd_view == "list") {
    *func = handle_list;
    return true;
  } else {
    return false;
  }
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
