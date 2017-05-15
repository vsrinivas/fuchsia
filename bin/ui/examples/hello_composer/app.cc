// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "apps/mozart/services/composer/composer.fidl.h"
#include "apps/mozart/services/composer/session.fidl.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/log_settings.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

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

    // Create the composer.
    app::ConnectToService(services_.get(), composer_.NewRequest());
  }

  void Update() {
    // Create a number of sessions, each of which will die after a fixed time.

    // Number of sessions to create.
    constexpr int kSessionCount = 16;
    // Number of milliseconds between creation of each session.
    constexpr int kSessionCreationInterval = 500;
    // Number of seconds before a session is closed.
    constexpr int kSessionDuration = 10;

    for (int i = 0; i < kSessionCount; ++i) {
      loop_->task_runner()->PostDelayedTask(
          [this]() {
            FTL_LOG(INFO) << "Creating new Session";
            mozart2::SessionPtr session;
            composer_->CreateSession(session.NewRequest());
            loop_->task_runner()->PostDelayedTask(
                ftl::MakeCopyable([session = std::move(session)]() {
                  // Allow SessionPtr to go out of scope, thus closing the
                  // session.
                  FTL_LOG(INFO) << "Closing session.";
                }),
                ftl::TimeDelta::FromSeconds(kSessionDuration));
          },
          ftl::TimeDelta::FromMilliseconds(kSessionCreationInterval * i));
    }
  }

 private:
  std::unique_ptr<app::ApplicationContext> application_context_;
  app::ApplicationControllerPtr controller_;
  app::ServiceProviderPtr services_;
  mozart2::ComposerPtr composer_;
  mtl::MessageLoop* loop_;
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
