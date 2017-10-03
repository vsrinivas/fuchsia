// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>

#include "lib/app/cpp/application_context.h"
#include "lib/app_driver/cpp/app_driver.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/macros.h"
#include "peridot/examples/hello_world_cpp/hello.fidl.h"

using examples::Hello;

namespace {

class HelloAppChild : public Hello {
 public:
  HelloAppChild(app::ApplicationContext* app_context) {
    app_context->outgoing_services()->AddService<Hello>(
        [this](fidl::InterfaceRequest<Hello> request) {
          hello_binding_.AddBinding(this, std::move(request));
        });
  }

  ~HelloAppChild() override = default;

  // Called by AppDriver.
  void Terminate(const std::function<void()>& done) { done(); }

 private:
  // |examples::Hello| implementation:
  void Say(const fidl::String& request, const SayCallback& callback) override {
    callback((request.get() == "hello") ? "hola!" : "adios!");
  }

  fidl::BindingSet<Hello> hello_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(HelloAppChild);
};

}  // namespace

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  auto app_context = app::ApplicationContext::CreateFromStartupInfo();
  modular::AppDriver<HelloAppChild> driver(
      app_context->outgoing_services(),
      std::make_unique<HelloAppChild>(app_context.get()),
      [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
