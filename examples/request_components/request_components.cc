// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "apps/modular/services/component/component.fidl.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

class RequestComponentApp {
 public:
  explicit RequestComponentApp(const std::string& component_id)
      : context_(app::ApplicationContext::CreateFromStartupInfo()) {
    component_index_ =
        context_->ConnectToEnvironmentService<component::ComponentIndex>();
    component_index_->GetComponent(
        component_id,
        [this](component::ComponentManifestPtr manifest,
               fidl::InterfaceHandle<component::ComponentResources> resources,
               network::NetworkErrorPtr error) {
          FXL_LOG(INFO) << "GetComponent returned.";
        });
  }

 private:
  std::unique_ptr<app::ApplicationContext> context_;
  fidl::InterfacePtr<component::ComponentIndex> component_index_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RequestComponentApp);
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  FXL_CHECK(argc == 2);
  RequestComponentApp app(argv[1]);
  loop.Run();
  return 0;
}
