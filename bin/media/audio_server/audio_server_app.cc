// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/audio_server/audio_server_app.h"

#include "apps/media/cpp/flog.h"
#include "apps/media/src/audio_server/audio_server_impl.h"
#include "lib/ftl/logging.h"

namespace media {
namespace audio {

AudioServerApp::AudioServerApp()
    : application_context_(
          modular::ApplicationContext::CreateFromStartupInfo()) {
  FTL_DCHECK(application_context_);

  FLOG_INITIALIZE(application_context_.get(), "audio_server");

  application_context_->outgoing_services()->AddService<AudioServer>(
      [this](fidl::InterfaceRequest<AudioServer> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

AudioServerApp::~AudioServerApp() {
  FLOG_DESTROY();
}

}  // namespace audio
}  // namespace media
