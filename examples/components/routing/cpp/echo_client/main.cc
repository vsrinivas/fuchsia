// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START imports]
#include <fidl/examples/routing/echo/cpp/fidl.h>
#include <lib/fidl/cpp/string.h>
#include <lib/sys/cpp/component_context.h>

#include <cstdlib>
#include <iostream>
#include <string>
// [END imports]

// [START main_body]
int main(int argc, const char* argv[], char* envp[]) {
  // [START_EXCLUDE silent]
  // TODO(fxbug.dev/97170): Consider migrating to async FIDL API
  // [END_EXCLUDE]
  // Connect to FIDL protocol
  fidl::examples::routing::echo::EchoSyncPtr echo_proxy;
  auto context = sys::ComponentContext::Create();
  context->svc()->Connect(echo_proxy.NewRequest());

  // Send messages over FIDL interface for each argument
  fidl::StringPtr response = nullptr;
  for (int i = 1; i < argc; i++) {
    ZX_ASSERT(echo_proxy->EchoString(argv[i], &response) == ZX_OK);
    if (!response.has_value()) {
      std::cout << "echo_string got empty result" << std::endl;
    } else {
      std::cout << "Server response: " << response->c_str() << std::endl;
    }
  }

  return 0;
}
// [END main_body]
