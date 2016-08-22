// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/logging.h"
#include "mojo/environment/scoped_chromium_init.h"
#include "mojo/public/c/system/main.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/services/flog/cpp/flog.h"
#include "services/media/audio/audio_server_app.h"

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
