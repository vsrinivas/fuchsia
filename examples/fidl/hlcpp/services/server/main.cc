// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/examples/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

class EchoImpl : public fuchsia::examples::Echo {
 public:
  explicit EchoImpl(bool reverse) : reverse_(reverse) {}

  void SendString(std::string value) override {}
  void EchoString(std::string value, EchoStringCallback callback) override {
    std::cout << "Got echo request" << std::endl;
    if (reverse_) {
      std::reverse(value.begin(), value.end());
    }
    callback(value);
  }

 private:
  const bool reverse_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  sys::ServiceHandler handler;
  fuchsia::examples::EchoService::Handler my_service(&handler);

  // Example of serving members of a service instance.
  EchoImpl regular_impl(false);
  fidl::BindingSet<fuchsia::examples::Echo> regular_echo_bindings;
  my_service.add_regular_echo(regular_echo_bindings.GetHandler(&regular_impl));

  EchoImpl reversed_impl(true);
  fidl::BindingSet<fuchsia::examples::Echo> reversed_echo_bindings;
  my_service.add_reversed_echo(reversed_echo_bindings.GetHandler(&reversed_impl));

  // Example of serving an instance of "EchoService".
  context->outgoing()->AddService<fuchsia::examples::EchoService>(std::move(handler));

  std::cout << "Running echo server" << std::endl;
  loop.Run();
  return 0;
}
