// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>

#include "src/connectivity/network/mdns/util/mdns_impl.h"
#include "src/connectivity/network/mdns/util/mdns_params.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/syslog/cpp/logger.h"

int main(int argc, const char** argv) {
  syslog::InitLogger({"mdns-util"});

  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  mdns::MdnsParams params(command_line);
  if (!params.is_valid()) {
    return 1;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::unique_ptr<sys::ComponentContext> component_context = sys::ComponentContext::Create();

  mdns::MdnsImpl impl(component_context.get(), &params, [&loop]() {
    async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); });
  });

  loop.Run();
  return 0;
}
