// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "local_codec_factory.h"

#include <lib/component/cpp/startup_context.h>

namespace codec_factory {

LocalCodecFactory::LocalCodecFactory() {}

void LocalCodecFactory::CreateSelfOwned(async_dispatcher_t* fidl_dispatcher) {
  auto startup_context = component::StartupContext::CreateFromStartupInfo();
  startup_context->outgoing_services()->AddServiceForName(
      [fidl_dispatcher](zx::channel request) {
        std::unique_ptr<LocalCodecFactory> codec_factory(
            new LocalCodecFactory());
        // C++ evaluation order is mostly arbitrary within a statement, so stash
        // this result of unique_ptr::operator->() to avoid moving the same
        // unique_ptr in a single statement.  The actual pointed-at instance
        // isn't moving, so it's fine to have this ref for a moment here.
        auto& binding = codec_factory->binding_;
        binding = std::make_unique<
            fidl::Binding<CodecFactory, std::unique_ptr<LocalCodecFactory>>>(
            std::move(codec_factory), std::move(request), fidl_dispatcher);
      },
      fuchsia::mediacodec::CodecFactory::Name_);
}

void LocalCodecFactory::CreateDecoder(
    fuchsia::mediacodec::CreateDecoder_Params decoder_params,
    ::fidl::InterfaceRequest<fuchsia::mediacodec::Codec> decoder_request) {
  // TODO(turnage): Implement
  ZX_ASSERT(false && "not yet implemented");
}

}  // namespace codec_factory