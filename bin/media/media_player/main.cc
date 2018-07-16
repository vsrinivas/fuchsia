// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <fs/pseudo-file.h>
#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/mediaplayer/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <trace-provider/provider.h>

#include "garnet/bin/media/media_player/media_player_impl.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/svc/cpp/services.h"

const std::string kIsolateUrl = "media_player";
const std::string kIsolateArgument = "--transient";

// Connects to the requested service in a media_player isolate.
template <typename Interface>
void ConnectToIsolate(fidl::InterfaceRequest<Interface> request,
                      fuchsia::sys::Launcher* launcher) {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kIsolateUrl;
  launch_info.arguments.push_back(kIsolateArgument);
  component::Services services;
  launch_info.directory_request = services.NewRequest();

  fuchsia::sys::ComponentControllerPtr controller;
  launcher->CreateComponent(std::move(launch_info), controller.NewRequest());

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
  trace::TraceProvider trace_provider(loop.dispatcher());

  std::unique_ptr<component::StartupContext> startup_context =
      component::StartupContext::CreateFromStartupInfo();

  if (transient) {
    std::unique_ptr<media_player::MediaPlayerImpl> player;
    startup_context->outgoing()
        .AddPublicService<fuchsia::mediaplayer::MediaPlayer>(
            [startup_context = startup_context.get(), &player,
             &loop](fidl::InterfaceRequest<fuchsia::mediaplayer::MediaPlayer>
                        request) {
              player = media_player::MediaPlayerImpl::Create(
                  std::move(request), startup_context, [&loop]() {
                    async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); });
                  });
            });

    loop.Run();
  } else {
    fuchsia::sys::LauncherPtr launcher;
    startup_context->environment()->GetLauncher(launcher.NewRequest());

    startup_context->outgoing()
        .AddPublicService<fuchsia::mediaplayer::MediaPlayer>(
            [&launcher](
                fidl::InterfaceRequest<fuchsia::mediaplayer::MediaPlayer>
                    request) {
              ConnectToIsolate<fuchsia::mediaplayer::MediaPlayer>(
                  std::move(request), launcher.get());
            });

    loop.Run();
  }

  return 0;
}
