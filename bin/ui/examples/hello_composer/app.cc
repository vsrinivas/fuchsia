// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mx/eventpair.h>

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "apps/mozart/lib/composer/session_helpers.h"
#include "apps/mozart/lib/composer/types.h"
#include "apps/mozart/services/composer/composer.fidl.h"
#include "apps/mozart/services/composer/ops.fidl.h"
#include "apps/mozart/services/composer/session.fidl.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/log_settings.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

using namespace mozart;

class HelloComposerApp {
 public:
  HelloComposerApp()
      : application_context_(app::ApplicationContext::CreateFromStartupInfo()),
        loop_(mtl::MessageLoop::GetCurrent()) {
    // Launch composer.
    auto launch_info = app::ApplicationLaunchInfo::New();
    launch_info->url = "file://system/apps/hello_composer_service";
    launch_info->services = services_.NewRequest();
    application_context_->launcher()->CreateApplication(
        std::move(launch_info), controller_.NewRequest());
    controller_.set_connection_error_handler([this] {
      FTL_LOG(INFO) << "Hello Composer service terminated.";
      loop_->QuitNow();
    });

    // Connect to the composer service.
    app::ConnectToService(services_.get(), composer_.NewRequest());
  }

  ResourceId NewResourceId() { return ++resource_id_counter_; }

  fidl::Array<mozart2::OpPtr> CreateLinkAndSampleScene() {
    auto ops = fidl::Array<mozart2::OpPtr>::New(0);

    // Create a Link to attach ourselves to.
    mx::eventpair link_handle1;
    mx::eventpair link_handle2;
    mx::eventpair::create(0, &link_handle1, &link_handle2);
    ResourceId link_id = NewResourceId();
    ops.push_back(NewCreateLinkOp(link_id, std::move(link_handle1)));

    // Create a red circle.
    ResourceId node_id = NewResourceId();
    ops.push_back(NewCreateShapeNodeOp(node_id));

    ResourceId material_id = NewResourceId();
    ops.push_back(NewCreateMaterialOp(material_id, 0, 255, 100, 100, 255));

    ResourceId shape_id = NewResourceId();
    ops.push_back(NewCreateCircleOp(shape_id, 50.f));

    ops.push_back(NewSetMaterialOp(node_id, material_id));
    ops.push_back(NewSetShapeOp(node_id, shape_id));

    // Translate the circle.
    float translation[3] = {50.f, 50.f, 10.f};
    ops.push_back(NewSetTransformOp(node_id, translation,
                                    kOnesFloat3,        // scale
                                    kZeroesFloat3,      // anchor point
                                    kQuaternionDefault  // rotation
                                    ));
    // Attach the circle to the Link.
    ops.push_back(NewAddChildOp(link_id, node_id));

    return ops;
  }

  void Update() {
    FTL_LOG(INFO) << "Creating new Session";
    mozart2::SessionPtr session;
    composer_->CreateSession(session.NewRequest());

    auto ops = CreateLinkAndSampleScene();

    session->Enqueue(std::move(ops));

    // Present
    // TODO: this does not do anything yet.
    session->Present(fidl::Array<mx::event>::New(0),
                     fidl::Array<mx::event>::New(0));

    session.set_connection_error_handler([this] {
      FTL_LOG(INFO) << "Session terminated.";
      loop_->QuitNow();
    });

    // Wait kSessionDuration seconds, and close the session.
    constexpr int kSessionDuration = 10;
    loop_->task_runner()->PostDelayedTask(
        ftl::MakeCopyable([session = std::move(session)]() {
          // Allow SessionPtr to go out of scope, thus closing the
          // session.
          FTL_LOG(INFO) << "Closing session.";
        }),
        ftl::TimeDelta::FromSeconds(kSessionDuration));
  }

 private:
  std::unique_ptr<app::ApplicationContext> application_context_;
  app::ApplicationControllerPtr controller_;
  app::ServiceProviderPtr services_;
  mozart2::ComposerPtr composer_;
  mtl::MessageLoop* loop_;
  ResourceId resource_id_counter_ = 0;
};

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (!ftl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  mtl::MessageLoop loop;
  HelloComposerApp app;
  loop.task_runner()->PostDelayedTask([&app] { app.Update(); },
                                      ftl::TimeDelta::FromSeconds(5));
  loop.task_runner()->PostDelayedTask(
      [&loop] {
        FTL_LOG(INFO) << "Quitting.";
        loop.QuitNow();
      },
      ftl::TimeDelta::FromSeconds(25));
  loop.Run();
  return 0;
}
