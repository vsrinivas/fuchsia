// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/examples/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/service/cpp/service.h>
#include <lib/sys/service/cpp/service_aggregate.h>

#include <iostream>

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  // Return a channel connected to the /svc directory.
  auto svc = context->svc()->CloneChannel();

  // Example of connecting to a member of a service instance.
  auto default_service = sys::OpenServiceAt<fuchsia::examples::EchoService>(svc);
  auto regular = default_service.regular_echo().Connect().Bind();

  // Example of listing instances of a service.
  auto service_aggregate = sys::OpenServiceAggregateAt<fuchsia::examples::EchoService>(svc);
  auto instance_names = service_aggregate.ListInstances();
  ZX_ASSERT(!instance_names.empty());
  auto service = sys::OpenServiceAt<fuchsia::examples::EchoService>(svc, instance_names[0]);
  auto reversed = service.reversed_echo().Connect().Bind();

  regular->EchoString("ping", [](fidl::StringPtr value) {
    std::cout << "Regular response: " << value << std::endl;
  });
  reversed->EchoString("pong", [&loop](fidl::StringPtr value) {
    std::cout << "Reversed response: " << value << std::endl;
    loop.Quit();
  });

  loop.Run();
  return 0;
}
