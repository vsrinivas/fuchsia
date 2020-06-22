// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fuchsia/examples/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

#include <iostream>

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  fuchsia::examples::EchoLauncherPtr echo_launcher;
  auto context = sys::ComponentContext::Create();
  context->svc()->Connect(echo_launcher.NewRequest());

  int num_responses = 0;

  // [START non-pipelined]
  fuchsia::examples::EchoPtr echo;
  auto callback = [&](fidl::InterfaceHandle<fuchsia::examples::Echo> client_end) {
    std::cout << "Got non pipelined response" << std::endl;
    echo.Bind(std::move(client_end));
    echo->EchoString("hello!", [&](std::string response) {
      std::cout << "Got echo response " << response << std::endl;
      if (++num_responses == 2) {
        loop.Quit();
      }
    });
  };
  echo_launcher->GetEcho("not pipelined: ", std::move(callback));
  // [END non-pipelined]

  // [START pipelined]
  fuchsia::examples::EchoPtr echo_pipelined;
  echo_launcher->GetEchoPipelined("pipelined: ", echo_pipelined.NewRequest());
  echo_pipelined->EchoString("hello!", [&](std::string response) {
    std::cout << "Got echo response " << response << std::endl;
    if (++num_responses == 2) {
      loop.Quit();
    }
  });
  // [END pipelined]

  loop.Run();
  return num_responses == 2 ? 0 : 1;
}
