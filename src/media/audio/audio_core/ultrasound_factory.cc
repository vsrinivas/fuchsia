// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/ultrasound_factory.h"

namespace media::audio {

std::unique_ptr<UltrasoundFactory> UltrasoundFactory::CreateAndServe(Context* context) {
  return std::make_unique<UltrasoundFactory>(context);
}

UltrasoundFactory::UltrasoundFactory(Context* context) : context_(*context) {
  FX_DCHECK(context);
  context_.component_context().outgoing()->AddPublicService(bindings_.GetHandler(this));
}

void UltrasoundFactory::CreateCapturer(
    fidl::InterfaceRequest<fuchsia::media::AudioCapturer> request,
    CreateCapturerCallback callback) {}

void UltrasoundFactory::CreateRenderer(
    fidl::InterfaceRequest<fuchsia::media::AudioRenderer> request,
    CreateRendererCallback callback) {}

}  // namespace media::audio
