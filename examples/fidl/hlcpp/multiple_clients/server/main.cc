// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/examples/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <stdio.h>

// [START impl]
class EchoImpl : public fuchsia::examples::Echo {
 public:
  void EchoString(std::string value, EchoStringCallback callback) override { callback(value); }
  void SendString(std::string value) override {}
};
// [END impl]

// [START main]
int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  EchoImpl impl;
  fidl::BindingSet<fuchsia::examples::Echo> bindings;
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  context->outgoing()->AddPublicService(bindings.GetHandler(&impl));

  printf("Running echo server\n");
  return loop.Run();
}
// [END main]
