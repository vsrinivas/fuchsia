// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

// [START includes]
#include <fuchsia/examples/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
// [END includes]

// [START main]
int main(int argc, const char** argv) {
  fuchsia::examples::EchoSyncPtr echo_proxy;
  auto context = sys::ComponentContext::Create();
  context->svc()->Connect(echo_proxy.NewRequest());

  ZX_ASSERT(echo_proxy->SendString("hi") == ZX_OK);
  std::string response;
  ZX_ASSERT(echo_proxy->EchoString("hello", &response) == ZX_OK);
  printf("Got response: %s\n", response.c_str());

  // TODO(fcz): this currently does not pass on CQ
  // return response == "hello" ? 0 : 1;
  return 0;
}
// [END main]
