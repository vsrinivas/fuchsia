// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>

#include "garnet/bin/guest/cli/launch.h"
#include "garnet/bin/guest/cli/list.h"
#include "garnet/bin/guest/cli/serial.h"
#include "garnet/bin/guest/cli/socat.h"
#include "lib/component/cpp/startup_context.h"
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

template <class T>
bool parse_number(const char* arg, const char* name, T* value,
                  fxl::Base base = fxl::Base::k10) {
  fxl::StringView arg_view(arg);
  if (!fxl::StringToNumberWithError(arg_view, value, base)) {
    std::cerr << "Invalid " << name << ": " << arg_view << "\n";
    return false;
  }
  return true;
}

static bool parse_args(int argc, const char** argv, async::Loop* loop,
                       component::StartupContext* context, CommandFunc* func) {
  if (argc < 2) {
    return false;
  }
  fxl::StringView cmd_view(argv[1]);
  if (cmd_view == "launch" && argc >= 3) {
    *func = [argc, argv, loop, context]() {
      handle_launch(argc - 2, argv + 2, loop, context);
    };
  } else if (cmd_view == "serial" && argc == 4) {
    uint32_t env_id;
    if (!parse_number(argv[2], "environment ID", &env_id))
      return false;
    uint32_t cid;
    if (!parse_number(argv[3], "context ID", &cid))
      return false;
    *func = [env_id, cid, loop, context]() {
      handle_serial(env_id, cid, loop, context);
    };
  } else if (cmd_view == "socat" && argc == 5) {
    uint32_t env_id;
    if (!parse_number(argv[2], "environment ID", &env_id))
      return false;
    uint32_t cid;
    if (!parse_number(argv[3], "context ID", &cid))
      return false;
    uint32_t port;
    if (!parse_number(argv[4], "port", &port))
      return false;
    *func = [env_id, cid, port, loop, context]() {
      handle_socat_connect(env_id, cid, port, loop, context);
    };
  } else if (cmd_view == "socat-listen" && argc == 4) {
    uint32_t env_id;
    if (!parse_number(argv[2], "environment ID", &env_id))
      return false;
    uint32_t host_port;
    if (!parse_number(argv[3], "host port", &host_port))
      return false;
    *func = [env_id, host_port, loop, context]() {
      handle_socat_listen(env_id, host_port, loop, context);
    };
  } else if (cmd_view == "list") {
    *func = [context]() { handle_list(context); };
    return true;
  } else {
    return false;
  }
  return true;
}

int main(int argc, const char** argv) {
  CommandFunc func;
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = component::StartupContext::CreateFromStartupInfoNotChecked();
  if (!parse_args(argc, argv, &loop, context.get(), &func)) {
    usage();
    return ZX_ERR_INVALID_ARGS;
  }

  func();
  return 0;
}
