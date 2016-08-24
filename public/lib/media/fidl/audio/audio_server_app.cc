// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/services/audio/audio_server_app.h"

#include "apps/media/cpp/flog.h"
#include "lib/ftl/logging.h"
#include "mojo/public/c/include/mojo/system/main.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"

namespace mojo {
namespace media {
namespace audio {

AudioServerApp::AudioServerApp() {}

AudioServerApp::~AudioServerApp() {
  FLOG_DESTROY();
}

void AudioServerApp::OnInitialize() {
  FLOG_INITIALIZE(shell(), "audio_service");
  server_impl_.Initialize();
}

bool AudioServerApp::OnAcceptConnection(
    ServiceProviderImpl* service_provider_impl) {
  service_provider_impl->AddService<AudioServer>(
      [this](const ConnectionContext& connection_context,
             InterfaceRequest<AudioServer> audio_server_request) {
        bindings_.AddBinding(&server_impl_, audio_server_request.Pass());
      });
  return true;
}

}  // namespace audio
}  // namespace media
}  // namespace mojo

MojoResult MojoMain(MojoHandle app_request) {
  mojo::ScopedChromiumInit init;
  mojo::media::audio::AudioServerApp audio_server_app;
  return mojo::RunApplication(app_request, &audio_server_app);
}
