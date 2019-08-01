// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <examples/fidl/gen/my_service.h>

class EchoImpl : public fidl::examples::echo::Echo {
 public:
  explicit EchoImpl(std::string label) : label_(std::move(label)) {}

  void EchoString(fidl::StringPtr value, EchoStringCallback callback) override {
    callback(label_ + (value.is_null() ? "(null)" : *value));
  }

 private:
  const std::string label_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = sys::ComponentContext::Create();
  fuchsia::examples::MyService::Handler handler;

  // Example of serving a member "foo" of a service instance.
  EchoImpl foo_impl("foo: ");
  fidl::BindingSet<fidl::examples::echo::Echo> foo_bindings;
  handler.add_foo(foo_bindings.GetHandler(&foo_impl));

  // Example of serving a member "bar" of a service instance.
  EchoImpl bar_impl("bar: ");
  fidl::BindingSet<fidl::examples::echo::Echo> bar_bindings;
  handler.add_bar(bar_bindings.GetHandler(&bar_impl));

  // Example of serving an instance of "MyService".
  context->outgoing()->AddService<fuchsia::examples::MyService>(std::move(handler));

  loop.Run();
  return 0;
}
