// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <stdio.h>
#include <zircon/status.h>

#include <iostream>

#include "src/developer/exception_broker/limbo_client/limbo_client.h"
#include "src/developer/exception_broker/limbo_client/options.h"

using namespace fuchsia::exception;

namespace {

void PrintError(zx_status_t status) {
  fprintf(stderr, "Could not communicate to limbo: %s\n", zx_status_get_string(status));
}

std::vector<const char*> ArgsToVec(int argc, const char* argv[]) {
  std::vector<const char*> args;
  args.reserve(argc);
  for (int i = 0; i < argc; i++) {
    args.push_back(argv[i]);
  }

  return args;
}

};  // namespace

int main(int argc, const char* argv[]) {
  OptionFunction func = ParseArgs(argc, argv, std::cout);
  if (!func)
    return EXIT_FAILURE;

  // Create the client.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto context = sys::ComponentContext::Create();
  auto& services = context->svc();

  LimboClient client(services);
  if (zx_status_t status = client.Init(); status != ZX_OK) {
    PrintError(status);
    return EXIT_FAILURE;
  }

  // Call the parsed function.
  if (zx_status_t status = func(&client, ArgsToVec(argc, argv), std::cout); status != ZX_OK) {
    fprintf(stderr, "Client exit status: %s.\n", zx_status_get_string(status));
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
