// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_ULTRASOUND_FACTORY_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_ULTRASOUND_FACTORY_H_

#include <fuchsia/ultrasound/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include "src/media/audio/audio_core/v1/context.h"

namespace media::audio {

class UltrasoundFactory : public fuchsia::ultrasound::Factory {
 public:
  static std::unique_ptr<UltrasoundFactory> CreateAndServe(Context* context);

  UltrasoundFactory(Context* context);

  void CreateCapturer(fidl::InterfaceRequest<fuchsia::media::AudioCapturer> request,
                      CreateCapturerCallback callback) override;
  void CreateRenderer(fidl::InterfaceRequest<fuchsia::media::AudioRenderer> request,
                      CreateRendererCallback callback) override;

 private:
  fidl::BindingSet<fuchsia::ultrasound::Factory> bindings_;
  Context& context_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_ULTRASOUND_FACTORY_H_
