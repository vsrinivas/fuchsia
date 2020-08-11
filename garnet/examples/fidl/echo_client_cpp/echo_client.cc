// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/examples/echo/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/default.h>
#include <lib/zx/process.h>
#include <zircon/processargs.h>

#include "echo_client_app.h"

int main(int argc, const char** argv) {
  std::string server_url = "fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx";
  std::string msg = "hello world";

  for (int i = 1; i < argc - 1; ++i) {
    if (!strcmp("--server", argv[i])) {
      server_url = argv[++i];
    } else if (!strcmp("-m", argv[i])) {
      msg = argv[++i];
    }
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  echo::EchoClientApp app;
  app.Start(server_url);

  app.echo()->EchoString(msg, [&loop](fidl::StringPtr value) {
    printf("***** Response: %s\n", value->data());
    loop.Quit();
  });
  app.echo().set_error_handler([&loop](zx_status_t status) {
    printf("Echo server closed connection: %d\n", status);
    loop.Quit();
  });
  return loop.Run();
}
