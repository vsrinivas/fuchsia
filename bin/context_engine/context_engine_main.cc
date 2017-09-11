// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/application_context.h"
#include "apps/maxwell/src/context_engine/context_engine_impl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/mtl/tasks/message_loop.h"

namespace maxwell {
namespace {

class App {
 public:
  App(app::ApplicationContext* app_context) {
    app_context->outgoing_services()->AddService<ContextEngine>(
        [this](fidl::InterfaceRequest<ContextEngine> request) {
          context_engine_impl_.AddBinding(std::move(request));
        });
  }

 private:
  ContextEngineImpl context_engine_impl_;
};

}  // namespace
}  // namespace maxwell

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  auto app_context = app::ApplicationContext::CreateFromStartupInfo();
  maxwell::App app(app_context.get());
  loop.Run();
  return 0;
}
