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
#include "src/virtualization/bin/vsh/vshc.h"

static void usage() {
  std::cerr << "Usage: vsh           [<env_id> [<cid> [<port>]]] [-c [<arg>...]]\n";
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

static bool parse_args(int argc, const char** argv, async::Loop* loop,
                       sys::ComponentContext* context, fit::function<zx_status_t()>* func) {
  if (argc < 1) {
    return false;
  }

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

  std::optional<uint32_t> port;
  bool success = true;
  switch (argc) {
    case 2:
      port = -1;
      success &= parse_number(argv[3], "port", &*port);
      __FALLTHROUGH;
    case 1:
      break;
    default:
      return false;
  }

  if (!success) {
    return false;
  }

  *func = [port, args, loop, context]() -> zx_status_t {
    return handle_vsh(port, args, loop, context);
  };
  return true;
}

int main(int argc, const char** argv) {
  fit::function<zx_status_t()> func;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  if (!parse_args(argc, argv, &loop, context.get(), &func)) {
    usage();
    return ZX_ERR_INVALID_ARGS;
  }

  return func() == ZX_OK ? 0 : 1;
}
