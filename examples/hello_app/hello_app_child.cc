// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>

#include "apps/modular/examples/hello_app/hello.fidl.h"
#include "apps/modular/lib/app/application_context.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

using examples::Hello;

namespace {

class HelloAppChild : public Hello {
 public:
  HelloAppChild()
      : context_(modular::ApplicationContext::CreateFromStartupInfo()) {
    context_->outgoing_services()->AddService<Hello>(
        [this](fidl::InterfaceRequest<Hello> request) {
          hello_binding_.AddBinding(this, std::move(request));
        });
  }

  ~HelloAppChild() override = default;

 private:
  // |examples::Hello| implementation:
  void Say(const fidl::String& request, const SayCallback& callback) override {
    callback((request.get() == "hello") ? "hola!" : "adios!");
  }

  std::unique_ptr<modular::ApplicationContext> context_;
  fidl::BindingSet<Hello> hello_binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(HelloAppChild);
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  HelloAppChild app;
  loop.Run();
  return 0;
}
