// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cpp/ui.h>

#include "lib/app/cpp/application_context.h"
#include "lib/ui/scenic/client/session.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"

namespace hello_spaces {

class App {
 public:
  explicit App();
  App(const App&) = delete;
  App& operator=(const App&) = delete;

 private:
  // Called asynchronously by constructor.
  void Init(gfx::DisplayInfo display_info);

  // Updates and presents the scene.  Called first by Init().  Each invocation
  // schedules another call to Update() when the result of the previous
  // presentation is asynchronously received.
  void Update(uint64_t next_presentation_time);

  // Called upon exit to free up the session and everything associated with it.
  void ReleaseSessionResources();

  std::unique_ptr<component::ApplicationContext> application_context_;
  fsl::MessageLoop* loop_;

  ui::ScenicPtr scenic_;
  std::unique_ptr<scenic_lib::Session> session_;
};

App::App()
    : application_context_(
          component::ApplicationContext::CreateFromStartupInfo()),
      loop_(fsl::MessageLoop::GetCurrent()) {
  // Connect to the SceneManager service.
  scenic_ = application_context_->ConnectToEnvironmentService<ui::Scenic>();
  scenic_.set_error_handler([this] {
    FXL_LOG(INFO) << "Lost connection to Scenic service.";
    loop_->QuitNow();
  });
  scenic_->GetDisplayInfo(
      [this](gfx::DisplayInfo display_info) { Init(std::move(display_info)); });
}

void App::Init(gfx::DisplayInfo display_info) {
  FXL_LOG(INFO) << "Creating new Session";

  // TODO: set up SessionListener.
  session_ = std::make_unique<scenic_lib::Session>(scenic_.get());
  session_->set_error_handler([this] {
    FXL_LOG(INFO) << "Session terminated.";
    loop_->QuitNow();
  });

  // Wait kSessionDuration seconds, and close the session.
  constexpr int kSessionDuration = 40;
  loop_->task_runner()->PostDelayedTask(
      [this] { ReleaseSessionResources(); },
      fxl::TimeDelta::FromSeconds(kSessionDuration));

  Update(zx_clock_get(ZX_CLOCK_MONOTONIC));
}

void App::Update(uint64_t next_presentation_time) {
  // Present.  Upon success, schedule the next frame's update.
  session_->Present(
      next_presentation_time, [this](images::PresentationInfo info) {
        Update(info.presentation_time + info.presentation_interval);
      });
}

void App::ReleaseSessionResources() {
  FXL_LOG(INFO) << "Closing session.";

  session_.reset();
}

}  // namespace hello_spaces


int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  fsl::MessageLoop loop;
  hello_spaces::App app;
  loop.task_runner()->PostDelayedTask(
      [&loop] {
        FXL_LOG(INFO) << "Quitting.";
        loop.QuitNow();
      },
      fxl::TimeDelta::FromSeconds(50));
  loop.Run();
  return 0;
}
