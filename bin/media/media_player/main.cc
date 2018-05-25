// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <fs/pseudo-file.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <media/cpp/fidl.h>
#include <media_player/cpp/fidl.h>
#include <trace-provider/provider.h>

#include "garnet/bin/media/media_player/media_player_impl.h"
#include "lib/app/cpp/application_context.h"
#include "lib/svc/cpp/services.h"

const std::string kIsolateUrl = "media_player";
const std::string kIsolateArgument = "--transient";

// Connects to the requested service in a media_player isolate.
template <typename Interface>
void ConnectToIsolate(fidl::InterfaceRequest<Interface> request,
                      component::ApplicationLauncher* launcher) {
  component::LaunchInfo launch_info;
  launch_info.url = kIsolateUrl;
  launch_info.arguments.push_back(kIsolateArgument);
  component::Services services;
  launch_info.directory_request = services.NewRequest();

  component::ComponentControllerPtr controller;
  launcher->CreateApplication(std::move(launch_info), controller.NewRequest());

  services.ConnectToService(std::move(request), Interface::Name_);

  controller->Detach();
}

int main(int argc, const char** argv) {
  bool transient = false;
  for (int arg_index = 0; arg_index < argc; ++arg_index) {
    if (argv[arg_index] == kIsolateArgument) {
      transient = true;
      break;
    }
  }

  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  trace::TraceProvider trace_provider(loop.async());

  std::unique_ptr<component::ApplicationContext> application_context =
      component::ApplicationContext::CreateFromStartupInfo();

  if (transient) {
    std::unique_ptr<media_player::MediaPlayerImpl> player;
    application_context->outgoing().AddPublicService<media_player::MediaPlayer>(
        [application_context = application_context.get(), &player,
         &loop](fidl::InterfaceRequest<media_player::MediaPlayer> request) {
          player = media_player::MediaPlayerImpl::Create(
              std::move(request), application_context, [&loop]() {
                async::PostTask(loop.async(), [&loop]() { loop.Quit(); });
              });
        });

    loop.Run();
  } else {
    component::ApplicationLauncherPtr launcher;
    application_context->environment()->GetApplicationLauncher(
        launcher.NewRequest());

    application_context->outgoing().AddPublicService<media_player::MediaPlayer>(
        [&launcher](fidl::InterfaceRequest<media_player::MediaPlayer> request) {
          ConnectToIsolate<media_player::MediaPlayer>(std::move(request),
                                                      launcher.get());
        });

    loop.Run();
  }

  return 0;
}
