// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>

#include <iostream>
#include <optional>

#include "lib/sys/cpp/component_context.h"
#include "src/lib/fxl/strings/string_number_conversions.h"
#include "src/virtualization/bin/guest/balloon.h"
#include "src/virtualization/bin/guest/launch.h"
#include "src/virtualization/bin/guest/list.h"
#include "src/virtualization/bin/guest/serial.h"
#include "src/virtualization/bin/guest/socat.h"
#include "src/virtualization/bin/guest/vshc.h"
#include "src/virtualization/bin/vmm/guest_config.h"

static void usage() {
  std::cerr << "Usage: guest <command> <package> <command-args>...\n"
            << "\n"
            << "Commands:\n"
            << "  balloon       <env_id> <cid> <num-pages>\n"
            << "  balloon-stats <env_id> <cid>\n"
            << "  launch        <package> <vmm-args>...\n"
            << "  list\n"
            << "  serial        <env_id> <cid>\n"
            << "  socat         <env_id> <cid> <port>\n"
            << "  socat-listen  <env_id> <host-port>\n"
            << "  vsh           [<env_id> [<cid> [<port>]]]\n";
}

template <class T>
static bool parse_number(const char* arg, const char* name, T* value,
                         fxl::Base base = fxl::Base::k10) {
  fxl::StringView arg_view(arg);
  if (!fxl::StringToNumberWithError(arg_view, value, base)) {
    std::cerr << "Invalid " << name << ": " << arg_view << "\n";
    return false;
  }
  return true;
}

static bool read_guest_cfg(int argc, const char** argv, fuchsia::virtualization::GuestConfig* cfg) {
  zx_status_t status = guest_config::ParseArguments(argc - 1, argv + 1, cfg);
  if (status != ZX_OK) {
    guest_config::PrintCommandLineUsage(argv[0]);
    std::cerr << "Invalid arguments\n";
    return false;
  }
  return true;
}

static bool parse_args(int argc, const char** argv, async::Loop* loop,
                       sys::ComponentContext* context, fit::closure* func) {
  if (argc < 1) {
    return false;
  }
  fxl::StringView cmd_view(argv[0]);
  if (cmd_view == "balloon" && argc == 4) {
    uint32_t env_id, cid, num_pages;
    if (!parse_number(argv[1], "environment ID", &env_id)) {
      return false;
    } else if (!parse_number(argv[2], "context ID", &cid)) {
      return false;
    } else if (!parse_number(argv[3], "number of pages", &num_pages)) {
      return false;
    }
    *func = [env_id, cid, num_pages, context]() {
      handle_balloon(env_id, cid, num_pages, context);
    };
  } else if (cmd_view == "balloon-stats" && argc == 3) {
    uint32_t env_id, cid;
    if (!parse_number(argv[1], "environment ID", &env_id)) {
      return false;
    } else if (!parse_number(argv[2], "context ID", &cid)) {
      return false;
    }
    *func = [env_id, cid, context]() { handle_balloon_stats(env_id, cid, context); };
  } else if (cmd_view == "launch" && argc >= 2) {
    *func = [argc, argv, loop, context]() mutable {
      fuchsia::virtualization::GuestConfig guest_config{};
      if (read_guest_cfg(argc, argv, &guest_config)) {
        handle_launch(argc - 1, argv + 1, loop, std::move(guest_config), context);
      }
    };
  } else if (cmd_view == "list") {
    *func = [context]() { handle_list(context); };
  } else if (cmd_view == "serial" && argc == 3) {
    uint32_t env_id, cid;
    if (!parse_number(argv[1], "environment ID", &env_id)) {
      return false;
    } else if (!parse_number(argv[2], "context ID", &cid)) {
      return false;
    }
    *func = [env_id, cid, loop, context]() { handle_serial(env_id, cid, loop, context); };
  } else if (cmd_view == "socat" && argc == 4) {
    uint32_t env_id, cid, port;
    if (!parse_number(argv[1], "environment ID", &env_id)) {
      return false;
    } else if (!parse_number(argv[2], "context ID", &cid)) {
      return false;
    } else if (!parse_number(argv[3], "port", &port)) {
      return false;
    }
    *func = [env_id, cid, port, loop, context]() {
      handle_socat_connect(env_id, cid, port, loop, context);
    };
  } else if (cmd_view == "socat-listen" && argc == 3) {
    uint32_t env_id, host_port;
    if (!parse_number(argv[1], "environment ID", &env_id)) {
      return false;
    } else if (!parse_number(argv[2], "host port", &host_port)) {
      return false;
    }
    *func = [env_id, host_port, loop, context]() {
      handle_socat_listen(env_id, host_port, loop, context);
    };
  } else if (cmd_view == "vsh" && (argc >= 1 && argc <= 4)) {
    std::optional<uint32_t> env_id, cid, port;

    bool success = true;
    switch (argc) {
      case 4:
        port = -1;
        success &= parse_number(argv[3], "port", &*port);
      case 3:
        cid = -1;
        success &= parse_number(argv[2], "context ID", &*cid);
      case 2:
        env_id = -1;
        success &= parse_number(argv[1], "environment ID", &*env_id);
      case 1:
        break;
      default:
        return false;
    }

    if (!success) {
      return false;
    }

    *func = [env_id, cid, port, loop, context]() { handle_vsh(env_id, cid, port, loop, context); };
  } else {
    return false;
  }
  return true;
}

int main(int argc, const char** argv) {
  fit::closure func;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  // This program might be called via an alias representing the vsh subcommand, e.g.
  // `guest vsh 0 3` vs `vsh 0 3`
  // Only if called using the long form, we must adjust argv for input to |parse_args|
  // so that argv[0] represents the subcommand name.
  if (fxl::StringView(argv[0]) == "guest") {
    argc--;
    argv++;
  }

  if (!parse_args(argc, argv, &loop, context.get(), &func)) {
    usage();
    return ZX_ERR_INVALID_ARGS;
  }

  func();
  return 0;
}
