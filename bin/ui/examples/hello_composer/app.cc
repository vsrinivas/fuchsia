// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "apps/mozart/services/composition2/composer.fidl.h"
#include "apps/mozart/services/composition2/session.fidl.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/log_settings.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

class HelloComposerApp {
 public:
  HelloComposerApp()
      : application_context_(app::ApplicationContext::CreateFromStartupInfo()),
        loop_(mtl::MessageLoop::GetCurrent()) {
    // Launch composer.
    app::ServiceProviderPtr services;
    auto launch_info = app::ApplicationLaunchInfo::New();
    launch_info->url = "file://system/apps/hello_composer_service";
    launch_info->services = services.NewRequest();
    app::ApplicationControllerPtr controller;
    application_context_->launcher()->CreateApplication(
        std::move(launch_info), controller.NewRequest());
    controller.set_connection_error_handler([this] {
      FTL_LOG(INFO) << "Hello Composer service terminated.";
      loop_->PostQuitTask();
    });

    // Create the composer.
    app::ConnectToService(services.get(), composer_.NewRequest());

    // Get a session
    composer_->CreateSession(session_.NewRequest());

    session_.set_connection_error_handler([this] {
      FTL_LOG(INFO) << "Session terminated.";
      loop_->PostQuitTask();
    });
  }

  void Update() { composer_ = nullptr; }

 private:
  std::unique_ptr<app::ApplicationContext> application_context_;
  mozart2::ComposerPtr composer_;
  mozart2::SessionPtr session_;
  mtl::MessageLoop* loop_;
};

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (!ftl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  mtl::MessageLoop loop;
  HelloComposerApp app;
  loop.task_runner()->PostTask([&app] { app.Update(); });
  loop.Run();
  return 0;
}
