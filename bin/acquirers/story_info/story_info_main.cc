// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/application_context.h"
#include "peridot/bin/acquirers/story_info/story_info.h"
#include "lib/agent/fidl/agent.fidl.h"
#include "lib/lifecycle/fidl/lifecycle.fidl.h"

#include "lib/fsl/tasks/message_loop.h"

namespace maxwell {

class StoryInfoApp : modular::Lifecycle {
 public:
  StoryInfoApp(app::ApplicationContext* app_context)
      : agent_binding_(&story_info_acquirer_), lifecycle_binding_(this) {
    app_context->outgoing_services()->AddService<modular::Agent>(
        [this](fidl::InterfaceRequest<modular::Agent> request) {
          FXL_DCHECK(!agent_binding_.is_bound());
          agent_binding_.Bind(std::move(request));
        });
    app_context->outgoing_services()->AddService<modular::Lifecycle>(
        [this](fidl::InterfaceRequest<modular::Lifecycle> request) {
          FXL_DCHECK(!lifecycle_binding_.is_bound());
          lifecycle_binding_.Bind(std::move(request));
        });
  }

 private:
  // |Lifecycle|
  void Terminate() override {
    agent_binding_.Close();
    fsl::MessageLoop::GetCurrent()->QuitNow();
  }

  StoryInfoAcquirer story_info_acquirer_;
  fidl::Binding<modular::Agent> agent_binding_;
  fidl::Binding<modular::Lifecycle> lifecycle_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryInfoApp);
};

}  // namespace maxwell

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  auto app_context = app::ApplicationContext::CreateFromStartupInfo();
  maxwell::StoryInfoApp app(app_context.get());
  loop.Run();
  return 0;
}
