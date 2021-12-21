// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>

#include <filesystem>
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
#include "src/virtualization/lib/guest_config/guest_config.h"

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
            << "  vsh           [<env_id> [<cid> [<port>]]] [-c [<arg>...]]\n";
}

template <class T>
static bool parse_number(const char* arg, const char* name, T* value,
                         fxl::Base base = fxl::Base::k10) {
  std::string_view arg_view(arg);
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
                       sys::ComponentContext* context, fit::function<zx_status_t()>* func) {
  if (argc < 1) {
    return false;
  }

  // In case the cmd is actually an absolute executable path take just the filename.
  auto cmd = std::filesystem::path(argv[0]).filename();

  if (cmd == "balloon" && argc == 4) {
    uint32_t env_id, cid, num_pages;
    if (!parse_number(argv[1], "environment ID", &env_id)) {
      return false;
    } else if (!parse_number(argv[2], "context ID", &cid)) {
      return false;
    } else if (!parse_number(argv[3], "number of pages", &num_pages)) {
      return false;
    }
    *func = [env_id, cid, num_pages, context]() -> zx_status_t {
      return handle_balloon(env_id, cid, num_pages, context);
    };
  } else if (cmd == "balloon-stats" && argc == 3) {
    uint32_t env_id, cid;
    if (!parse_number(argv[1], "environment ID", &env_id)) {
      return false;
    } else if (!parse_number(argv[2], "context ID", &cid)) {
      return false;
    }
    *func = [env_id, cid, context]() -> zx_status_t {
      return handle_balloon_stats(env_id, cid, context);
    };
  } else if (cmd == "launch" && argc >= 2) {
    *func = [argc, argv, loop, context]() mutable -> zx_status_t {
      fuchsia::virtualization::GuestConfig cfg;
      if (!read_guest_cfg(argc, argv, &cfg)) {
        return ZX_ERR_INVALID_ARGS;
      }
      return handle_launch(argc - 1, argv + 1, loop, std::move(cfg), context);
    };
  } else if (cmd == "list") {
    *func = [context]() -> zx_status_t { return handle_list(context); };
  } else if (cmd == "serial" && argc == 3) {
    uint32_t env_id, cid;
    if (!parse_number(argv[1], "environment ID", &env_id)) {
      return false;
    } else if (!parse_number(argv[2], "context ID", &cid)) {
      return false;
    }
    *func = [env_id, cid, loop, context]() -> zx_status_t {
      return handle_serial(env_id, cid, loop, context);
    };
  } else if (cmd == "socat" && argc == 4) {
    uint32_t env_id, cid, port;
    if (!parse_number(argv[1], "environment ID", &env_id)) {
      return false;
    } else if (!parse_number(argv[2], "context ID", &cid)) {
      return false;
    } else if (!parse_number(argv[3], "port", &port)) {
      return false;
    }
    *func = [env_id, cid, port, loop, context]() -> zx_status_t {
      return handle_socat_connect(env_id, cid, port, loop, context);
    };
  } else if (cmd == "socat-listen" && argc == 3) {
    uint32_t env_id, host_port;
    if (!parse_number(argv[1], "environment ID", &env_id)) {
      return false;
    } else if (!parse_number(argv[2], "host port", &host_port)) {
      return false;
    }
    *func = [env_id, host_port, loop, context]() -> zx_status_t {
      return handle_socat_listen(env_id, host_port, loop, context);
    };
  } else if (cmd == "vsh") {
    std::vector<std::string> args;
    int args_start = argc;
    bool found_container_args = false;
    for (int i = 1; i < argc; i++) {
      if (found_container_args) {
        args.push_back(argv[i]);
      } else if (std::string_view{argv[i]} == "-c") {
        args_start = i;
        found_container_args = true;
      }
    }

    if (found_container_args) {
      if (args.empty()) {
        args = {"lxc", "exec", "penguin", "--", "login", "-f", "machina"};
      } else {
        args.insert(args.begin(), {"lxc", "exec", "penguin", "--"});
      }
    }

    // Truncate the effective argv under later consideration.
    argc = args_start;

    std::optional<uint32_t> env_id, cid, port;
    bool success = true;
    switch (argc) {
      case 4:
        port = -1;
        success &= parse_number(argv[3], "port", &*port);
        __FALLTHROUGH;
      case 3:
        cid = -1;
        success &= parse_number(argv[2], "context ID", &*cid);
        __FALLTHROUGH;
      case 2:
        env_id = -1;
        success &= parse_number(argv[1], "environment ID", &*env_id);
        __FALLTHROUGH;
      case 1:
        break;
      default:
        return false;
    }

    if (!success) {
      return false;
    }

    *func = [env_id, cid, port, args, loop, context]() -> zx_status_t {
      return handle_vsh(env_id, cid, port, args, loop, context);
    };
  } else {
    return false;
  }
  return true;
}

int main(int argc, const char** argv) {
  fit::function<zx_status_t()> func;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  // This program might be called via an alias representing the vsh subcommand, e.g.
  // `guest vsh 0 3` vs `vsh 0 3`
  // Only if called using the long form, we must adjust argv for input to |parse_args|
  // so that argv[0] represents the subcommand name.
  if (std::string_view(argv[0]) == "guest") {
    argc--;
    argv++;
  }

  if (!parse_args(argc, argv, &loop, context.get(), &func)) {
    usage();
    return ZX_ERR_INVALID_ARGS;
  }

  return func() == ZX_OK ? 0 : 1;
}
