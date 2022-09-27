// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/effects_loader_v2.h"

#include <lib/fit/defer.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <vector>

namespace media::audio {

fpromise::result<std::unique_ptr<EffectsLoaderV2>, zx_status_t> EffectsLoaderV2::CreateFromContext(
    const sys::ComponentContext& component_context) {
  TRACE_DURATION("audio", "EffectsLoaderV2::CreateFromContext");
  auto svc =
      fidl::ClientEnd<fuchsia_io::Directory>(component_context.svc()->CloneChannel().TakeChannel());
  auto client_end = component::ConnectAt<fuchsia_audio_effects::ProcessorCreator>(svc);
  if (!client_end.is_ok()) {
    return fpromise::error(client_end.status_value());
  }

  return CreateFromChannel(std::move(*client_end));
}

fpromise::result<std::unique_ptr<EffectsLoaderV2>, zx_status_t> EffectsLoaderV2::CreateFromChannel(
    fidl::ClientEnd<fuchsia_audio_effects::ProcessorCreator> creator) {
  TRACE_DURATION("audio", "EffectsLoaderV2::CreateFromChannel");
  return fpromise::ok(std::unique_ptr<EffectsLoaderV2>(new EffectsLoaderV2(std::move(creator))));
}

fidl::WireResult<fuchsia_audio_effects::ProcessorCreator::Create>
EffectsLoaderV2::GetProcessorConfiguration(std::string name) {
  return creator_->Create(fidl::StringView::FromExternal(name));
}

}  // namespace media::audio
