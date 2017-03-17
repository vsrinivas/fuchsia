// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "apps/maxwell/services/user/user_intelligence_provider.fidl.h"
#include "apps/maxwell/src/user/user_intelligence_provider_impl.h"
#include "lib/mtl/tasks/message_loop.h"

namespace maxwell {
namespace {

class App {
 public:
  App(app::ApplicationContext* app_context) : factory_impl_(app_context) {
    auto services = app_context->outgoing_services();
    services->AddService<UserIntelligenceProviderFactory>(
        [this](
            fidl::InterfaceRequest<UserIntelligenceProviderFactory> request) {
          factory_bindings_.AddBinding(&factory_impl_, std::move(request));
        });
  }

 private:
  UserIntelligenceProviderFactoryImpl factory_impl_;
  fidl::BindingSet<UserIntelligenceProviderFactory> factory_bindings_;
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