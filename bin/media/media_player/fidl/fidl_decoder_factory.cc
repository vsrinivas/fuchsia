// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/mediacodec/cpp/fidl.h>

#include "garnet/bin/media/media_player/fidl/fidl_decoder_factory.h"

#include "garnet/bin/media/media_player/fidl/fidl_decoder.h"
#include "garnet/bin/media/media_player/fidl/fidl_type_conversions.h"
#include "lib/fidl/cpp/clone.h"

namespace media_player {

// static
std::unique_ptr<DecoderFactory> FidlDecoderFactory::Create(
    component::StartupContext* startup_context) {
  return std::make_unique<FidlDecoderFactory>(startup_context);
}

FidlDecoderFactory::FidlDecoderFactory(
    component::StartupContext* startup_context) {
  codec_factory_ =
      startup_context
          ->ConnectToEnvironmentService<fuchsia::mediacodec::CodecFactory>();
}

FidlDecoderFactory::~FidlDecoderFactory() {}

void FidlDecoderFactory::CreateDecoder(
    const StreamType& stream_type,
    fit::function<void(std::shared_ptr<Decoder>)> callback) {
  FXL_DCHECK(callback);

  auto format_details =
      fxl::To<fuchsia::mediacodec::CodecFormatDetailsPtr>(stream_type);
  if (!format_details) {
    // If we don't know how to build |CodecFormatDetails|, we don't know how
    // to make a decoder.
    callback(nullptr);
    return;
  }

  fuchsia::mediacodec::CreateDecoder_Params decoder_params;
  decoder_params.input_details = fidl::Clone(*format_details);
  decoder_params.promise_separate_access_units_on_input = true;

  FXL_DCHECK(codec_factory_);
  fuchsia::mediacodec::CodecPtr decoder;
  codec_factory_->CreateDecoder(std::move(decoder_params),
                                decoder.NewRequest());
  FXL_DCHECK(decoder);

  FidlDecoder::Create(std::move(*format_details), std::move(decoder),
                      std::move(callback));
}

}  // namespace media_player
