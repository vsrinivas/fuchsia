// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <lib/fit/function.h>

#include "garnet/bin/guest/cli/dump.h"
#include "garnet/bin/guest/cli/launch.h"
#include "garnet/bin/guest/cli/list.h"
#include "garnet/bin/guest/cli/serial.h"
#include "garnet/bin/guest/cli/socat.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/fxl/strings/string_printf.h"

using CommandFunc = fit::closure;

static void usage() {
  std::cerr << "Usage: guest <command> <package> <command-args>...\n"
            << "\n"
            << "Commands:\n"
            << "  launch        <package> <vmm-args>...\n"
            << "  list\n"
            << "  serial        <env_id> <cid>\n"
            << "  socat         <env_id> <cid> <port>\n"
            << "  socat-listen  <env_id> <host-port>\n"
            << "  dump          <env_id> <cid> <hex-addr> <hex-len>\n";
}

bool parse_id(const char* arg, uint32_t* id) {
  fxl::StringView id_view(arg);
  if (!fxl::StringToNumberWithError(id_view, id)) {
    std::cerr << "Invalid id " << id << "\n";
    return false;
  }
  return true;
}

static bool parse_args(int argc, const char** argv, CommandFunc* func) {
  if (argc < 2) {
    return false;
  }
  fxl::StringView cmd_view(argv[1]);
  if (cmd_view == "dump" && argc == 6) {
    uint32_t env_id;
    if (!parse_id(argv[2], &env_id)) {
      return false;
    }
    uint32_t cid;
    if (!parse_id(argv[3], &cid)) {
      return false;
    }
    fxl::StringView addr_view(argv[4]);
    zx_vaddr_t addr;
    if (!fxl::StringToNumberWithError(addr_view, &addr, fxl::Base::k16)) {
      std::cerr << "Invalid address " << addr_view << "\n";
      return false;
    }
    fxl::StringView len_view(argv[5]);
    size_t len;
    if (!fxl::StringToNumberWithError(len_view, &len, fxl::Base::k16)) {
      std::cerr << "Invalid length " << len_view << "\n";
      return false;
    }
    *func = [env_id, cid, addr, len] { handle_dump(env_id, cid, addr, len); };
  } else if (cmd_view == "launch" && argc >= 3) {
    *func = [argc, argv]() { handle_launch(argc - 2, argv + 2); };
  } else if (cmd_view == "serial" && argc == 4) {
    uint32_t env_id;
    if (!parse_id(argv[2], &env_id)) {
      return false;
    }
    uint32_t cid;
    if (!parse_id(argv[3], &cid)) {
      return false;
    }
    *func = [env_id, cid]() { handle_serial(env_id, cid); };
  } else if ((cmd_view == "socat" || cmd_view == "socat-listen") && argc >= 4) {
    uint32_t env_id;
    fxl::StringView env_id_view(argv[2]);
    if (!fxl::StringToNumberWithError(env_id_view, &env_id)) {
      std::cerr << "Invalid environment ID: " << env_id_view << "\n";
      return false;
    }

    if (cmd_view == "socat" && argc == 5) {
      uint32_t cid;
      fxl::StringView cid_view(argv[3]);
      if (!fxl::StringToNumberWithError(cid_view, &cid)) {
        std::cerr << "Invalid context ID: " << cid_view << "\n";
        return false;
      }
      uint32_t port;
      fxl::StringView port_view(argv[4]);
      if (!fxl::StringToNumberWithError(port_view, &port)) {
        std::cerr << "Invalid port: " << port_view << "\n";
        return false;
      }
      *func = [env_id, cid, port]() {
        handle_socat_connect(env_id, cid, port);
      };
      return true;
    } else if (cmd_view == "socat-listen" && argc == 4) {
      uint32_t host_port;
      fxl::StringView host_port_view(argv[3]);
      if (!fxl::StringToNumberWithError(host_port_view, &host_port)) {
        std::cerr << "Invalid port: " << host_port_view << "\n";
        return false;
      }
      *func = [env_id, host_port]() { handle_socat_listen(env_id, host_port); };
      return true;
    }
    return false;
  } else if (cmd_view == "list") {
    *func = handle_list;
    return true;
  } else {
    return false;
  }
  return true;
}

int main(int argc, const char** argv) {
  CommandFunc func;
  if (!parse_args(argc, argv, &func)) {
    usage();
    return ZX_ERR_INVALID_ARGS;
  }

  func();
  return 0;
}
