// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/ultrasound_factory.h"

#include "src/media/audio/audio_core/v1/route_graph.h"
#include "src/media/audio/audio_core/v1/ultrasound_capturer.h"
#include "src/media/audio/audio_core/v1/ultrasound_renderer.h"

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
    CreateCapturerCallback callback) {
  auto capturer = UltrasoundCapturer::Create(std::move(request), &context_, std::move(callback));
  // Ultrasound capturers are immediately routable.
  context_.route_graph().AddCapturer(capturer);
  context_.route_graph().SetCapturerRoutingProfile(
      *capturer, RoutingProfile{.routable = true,
                                .usage = StreamUsage::WithCaptureUsage(CaptureUsage::ULTRASOUND)});
}

void UltrasoundFactory::CreateRenderer(
    fidl::InterfaceRequest<fuchsia::media::AudioRenderer> request,
    CreateRendererCallback callback) {
  auto renderer = UltrasoundRenderer::Create(std::move(request), &context_, std::move(callback));
  // Ultrasound renderers are immediately routable.
  context_.route_graph().AddRenderer(renderer);
  context_.route_graph().SetRendererRoutingProfile(
      *renderer, RoutingProfile{
                     .routable = true,
                     .usage = StreamUsage::WithRenderUsage(RenderUsage::ULTRASOUND),
                 });
}

}  // namespace media::audio
