// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/examples/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/sys/cpp/component_context.h>
#include <stdio.h>

#include <iostream>

constexpr int kNumClients = 3;

// [START main]
int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto context = sys::ComponentContext::Create();

  fidl::InterfacePtrSet<fuchsia::examples::Echo> echoers;
  for (int i = 0; i < kNumClients; i++) {
    fuchsia::examples::EchoPtr proxy;
    context->svc()->Connect(proxy.NewRequest());
    proxy.set_error_handler([&loop](zx_status_t status) {
      std::cout << "Error reading incoming message: " << status << std::endl;
      loop.Quit();
    });
    echoers.AddInterfacePtr(std::move(proxy));
  }

  size_t responses = 0;
  for (auto& echoer : echoers.ptrs()) {
    (*echoer)->EchoString("Hello echoer " + std::to_string(responses++), [&](std::string response) {
      std::cout << "Got response " << response << std::endl;
      if (responses == echoers.size()) {
        loop.Quit();
      }
    });
  }

  loop.Run();
  return responses == kNumClients ? 0 : 1;
}
// [END main]
