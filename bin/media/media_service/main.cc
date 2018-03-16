// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <trace-provider/provider.h>

#include "garnet/bin/media/media_service/media_component_factory.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/media/fidl/media_player.fidl.h"
#include "lib/svc/cpp/services.h"

const std::string kIsolateUrl = "media_service";
const std::string kIsolateArgument = "--transient";

// Connects to the requested service in a media_service isolate.
template <typename Interface>
void ConnectToIsolate(f1dl::InterfaceRequest<Interface> request,
                      app::ApplicationLauncher* launcher) {
  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->url = kIsolateUrl;
  launch_info->arguments.push_back(kIsolateArgument);
  app::Services services;
  launch_info->directory_request = services.NewRequest();

  app::ApplicationControllerPtr controller;
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

  fsl::MessageLoop loop;
  trace::TraceProvider trace_provider(loop.async());

  std::unique_ptr<app::ApplicationContext> application_context =
      app::ApplicationContext::CreateFromStartupInfo();

  if (transient) {
    media::MediaComponentFactory factory(std::move(application_context));

    factory.application_context()
        ->outgoing_services()
        ->AddService<media::MediaPlayer>(
            [&factory](f1dl::InterfaceRequest<media::MediaPlayer> request) {
              factory.CreateMediaPlayer(std::move(request));
            });

    loop.Run();
  } else {
    app::ApplicationLauncherPtr launcher;
    application_context->environment()->GetApplicationLauncher(
        launcher.NewRequest());

    application_context->outgoing_services()->AddService<media::MediaPlayer>(
        [&launcher](f1dl::InterfaceRequest<media::MediaPlayer> request) {
          ConnectToIsolate<media::MediaPlayer>(std::move(request),
                                               launcher.get());
        });

    loop.Run();
  }

  return 0;
}
