// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>

#include <fuchsia/cpp/hello_world_module.h>
#include "lib/app/cpp/application_context.h"
#include "lib/app_driver/cpp/app_driver.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/macros.h"

namespace {

class HelloAppChild : public hello_world_module::Hello {
 public:
  HelloAppChild(component::ApplicationContext* app_context) {
    app_context->outgoing_services()->AddService<hello_world_module::Hello>(
        [this](fidl::InterfaceRequest<hello_world_module::Hello> request) {
          hello_binding_.AddBinding(this, std::move(request));
        });
  }

  ~HelloAppChild() override = default;

  // Called by AppDriver.
  void Terminate(const std::function<void()>& done) { done(); }

 private:
  // |hello_world_module::Hello| implementation:
  void Say(fidl::StringPtr request, SayCallback callback) override {
    callback((request.get() == "hello") ? "hola!" : "adios!");
  }

  fidl::BindingSet<hello_world_module::Hello> hello_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(HelloAppChild);
};

}  // namespace

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  auto app_context = component::ApplicationContext::CreateFromStartupInfo();
  modular::AppDriver<HelloAppChild> driver(
      app_context->outgoing_services(),
      std::make_unique<HelloAppChild>(app_context.get()),
      [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
