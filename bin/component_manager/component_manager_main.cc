// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/application_context.h"
#include "apps/modular/services/component/component.fidl.h"
#include "apps/modular/src/component_manager/component_index_impl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fsl/tasks/message_loop.h"

namespace component {

class App {
 public:
  App()
      : context_(app::ApplicationContext::CreateFromStartupInfo()),
        impl_(
            context_->ConnectToEnvironmentService<network::NetworkService>()) {
    context_->outgoing_services()->AddService<ComponentIndex>(
        [this](fidl::InterfaceRequest<ComponentIndex> request) {
          bindings_.AddBinding(&impl_, std::move(request));
        });
  }

 private:
  std::unique_ptr<app::ApplicationContext> context_;
  ComponentIndexImpl impl_;
  fidl::BindingSet<ComponentIndex> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace component

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  component::App app;
  loop.Run();
  return 0;
}
