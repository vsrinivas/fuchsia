// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

// [START includes]
#include <fuchsia/examples/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
// [END includes]

// [START main]
int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  fuchsia::examples::EchoPtr echo_proxy;
  auto context = sys::ComponentContext::Create();
  context->svc()->Connect(echo_proxy.NewRequest());

  echo_proxy.set_error_handler([&loop](zx_status_t status) {
    printf("Error reading incoming message: %d\n", status);
    loop.Quit();
  });

  int num_responses = 0;
  echo_proxy->SendString("hi");
  echo_proxy->EchoString("hello", [&](std::string response) {
    printf("Got response %s\n", response.c_str());
    if (++num_responses == 2) {
      loop.Quit();
    }
  });
  echo_proxy.events().OnString = [&](std::string response) {
    printf("Got event %s\n", response.c_str());
    if (++num_responses == 2) {
      loop.Quit();
    }
  };

  loop.Run();
  return num_responses == 2 ? 0 : 1;
}
// [END main]
