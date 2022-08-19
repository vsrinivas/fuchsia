// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/component/decl/cpp/fidl.h>
#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/media/playback/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/trace-provider/provider.h>

#include <string>

#include "lib/fidl/cpp/interface_request.h"
#include "src/lib/storage/vfs/cpp/pseudo_file.h"
#include "src/media/playback/mediaplayer/audio_consumer_impl.h"
#include "src/media/playback/mediaplayer/player_impl.h"
const std::string kIsolateUrl = "fuchsia-pkg://fuchsia.com/mediaplayer#meta/mediaplayer.cm";
const std::string kIsolateArgument = "--transient";
template <typename Interface>
void ConnectDynamicChild(std::string child_name, fidl::InterfaceRequest<Interface> request,
                         fuchsia::component::RealmPtr &realm_proxy) {
  fuchsia::component::decl::ChildRef child_ref = {
      .name = child_name,
      .collection = "isolates",
  };
  fidl::InterfaceHandle<fuchsia::io::Directory> exposed_dir;
  realm_proxy->OpenExposedDir(
      child_ref, exposed_dir.NewRequest(),
      [exposed_dir = std::move(exposed_dir), request = std::move(request)](
          fuchsia::component::Realm_OpenExposedDir_Result result) mutable {
        if (result.is_err()) {
          FX_LOGS(ERROR) << "Failed to open exposed dir of mediaplayer isolate.";
          return;
        }
        std::shared_ptr<sys::ServiceDirectory> svc =
            std::make_shared<sys::ServiceDirectory>(sys::ServiceDirectory(std::move(exposed_dir)));
        svc->Connect(std::move(request));
      });
}
// Connects to the requested service in a mediaplayer isolate.
template <typename Interface>
void CreateDynamicChild(fidl::InterfaceRequest<Interface> request,
                        fuchsia::component::RealmPtr &realm_proxy) {
  fuchsia::component::decl::CollectionRef collection_ref = {
      .name = "isolates",
  };
  fuchsia::component::decl::Child child_decl;
  static int counter = 0;
  std::string child_name = "isolate_dynamic" + std::to_string(counter++);
  child_decl.set_name(child_name);
  child_decl.set_url("#meta/mediaplayer_isolate.cm");
  child_decl.set_startup(fuchsia::component::decl::StartupMode::LAZY);
  realm_proxy->CreateChild(
      std::move(collection_ref), std::move(child_decl), fuchsia::component::CreateChildArgs(),
      [child_name, &realm_proxy,
       request = std::move(request)](fuchsia::component::Realm_CreateChild_Result result) mutable {
        if (result.is_err()) {
          FX_LOGS(ERROR) << "Failed to create dynamic child of mediaplayer isolate.";
          return;
        }
        FX_LOGS(INFO) << "Dynamic child instance created.";
        ConnectDynamicChild(child_name, std::move(request), realm_proxy);
      });
}
int main(int argc, const char **argv) {
  syslog::SetTags({"mediaplayer"});
  bool transient = false;
  for (int arg_index = 0; arg_index < argc; ++arg_index) {
    if (argv[arg_index] == kIsolateArgument) {
      transient = true;
      break;
    }
  }
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  std::unique_ptr<sys::ComponentContext> component_context =
      sys::ComponentContext::CreateAndServeOutgoingDirectory();
  fuchsia::component::RealmPtr realm_proxy =
      component_context->svc()->Connect<fuchsia::component::Realm>();
  std::unique_ptr<media_player::SessionAudioConsumerFactoryImpl> factory;
  std::unique_ptr<media_player::PlayerImpl> player;
  if (transient) {
    component_context->outgoing()->AddPublicService<fuchsia::media::playback::Player>(
        [component_context = component_context.get(), &player,
         &loop](fidl::InterfaceRequest<fuchsia::media::playback::Player> request) {
          player = media_player::PlayerImpl::Create(
              std::move(request), component_context,
              [&loop]() { async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); }); });
        });
    component_context->outgoing()->AddPublicService<fuchsia::media::SessionAudioConsumerFactory>(
        [component_context = component_context.get(), &factory,
         &loop](fidl::InterfaceRequest<fuchsia::media::SessionAudioConsumerFactory> request) {
          factory = media_player::SessionAudioConsumerFactoryImpl::Create(
              std::move(request), component_context,
              [&loop]() { async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); }); });
        });
  } else {
    component_context->outgoing()->AddPublicService<fuchsia::media::playback::Player>(
        [&realm_proxy](fidl::InterfaceRequest<fuchsia::media::playback::Player> request) {
          CreateDynamicChild(std::move(request), realm_proxy);
        });
    component_context->outgoing()->AddPublicService<fuchsia::media::SessionAudioConsumerFactory>(
        [&realm_proxy](
            fidl::InterfaceRequest<fuchsia::media::SessionAudioConsumerFactory> request) {
          CreateDynamicChild(std::move(request), realm_proxy);
        });
  }
  loop.Run();
  return 0;
}
