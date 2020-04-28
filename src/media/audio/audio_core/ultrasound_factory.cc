// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/ultrasound_factory.h"

#include "src/media/audio/audio_core/route_graph.h"
#include "src/media/audio/audio_core/ultrasound_renderer.h"

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
    CreateRendererCallback callback) {
  auto renderer =
      std::make_unique<UltrasoundRenderer>(std::move(request), &context_, std::move(callback));
  auto renderer_raw = renderer.get();
  // Ultrasound renderers are immediately routable.
  context_.route_graph().AddRenderer(std::move(renderer));
  context_.route_graph().SetRendererRoutingProfile(
      *renderer_raw, RoutingProfile{
                         .routable = true,
                         .usage = StreamUsage::WithRenderUsage(RenderUsage::ULTRASOUND),
                     });
}

}  // namespace media::audio
