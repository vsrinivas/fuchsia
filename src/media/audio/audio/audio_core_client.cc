// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio/audio_core_client.h"

#include "src/lib/syslog/cpp/logger.h"

namespace media::audio {
AudioCoreClient::AudioCoreClient(sys::ComponentContext* component_context,
                                 fit::closure quit_callback)
    : quit_callback_(std::move(quit_callback)) {
  audio_core_.set_error_handler([this](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Connection to fuchsia.media.AudioCore failed: ";
    quit_callback_();
  });

  component_context->svc()->Connect(audio_core_.NewRequest());

  component_context->outgoing()->AddPublicService<fuchsia::media::Audio>(
      [this](fidl::InterfaceRequest<fuchsia::media::Audio> request) {
        bindings_.AddBinding(this,
                             fidl::InterfaceRequest<fuchsia::media::Audio>(std::move(request)));
      });
}

}  // namespace media::audio
