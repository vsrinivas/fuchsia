// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "local_single_codec_factory.h"

#include <lib/component/cpp/startup_context.h>

#include "codec_adapter_ffmpeg_decoder.h"
#include "codec_adapter_ffmpeg_encoder.h"

LocalSingleCodecFactory::LocalSingleCodecFactory(
    async_dispatcher_t* fidl_dispatcher,
    fidl::InterfaceRequest<CodecFactory> request,
    fit::function<void(std::unique_ptr<CodecImpl>)> factory_done_callback,
    CodecAdmissionControl* codec_admission_control,
    fit::function<void(zx_status_t)> error_handler)
    : fidl_dispatcher_(fidl_dispatcher),
      binding_(this),
      factory_done_callback_(std::move(factory_done_callback)),
      codec_admission_control_(codec_admission_control) {
  binding_.set_error_handler(std::move(error_handler));
  zx_status_t status = binding_.Bind(std::move(request), fidl_dispatcher);
  ZX_ASSERT(status == ZX_OK);
}

void LocalSingleCodecFactory::CreateDecoder(
    fuchsia::mediacodec::CreateDecoder_Params decoder_params,
    ::fidl::InterfaceRequest<fuchsia::media::StreamProcessor> decoder_request) {
  VendCodecAdapter<CodecAdapterFfmpegDecoder>(std::move(decoder_params),
                                              std::move(decoder_request));
}

void LocalSingleCodecFactory::CreateEncoder(
    fuchsia::mediacodec::CreateEncoder_Params encoder_params,
    ::fidl::InterfaceRequest<fuchsia::media::StreamProcessor> encoder_request) {
  VendCodecAdapter<CodecAdapterFfmpegEncoder>(std::move(encoder_params),
                                              std::move(encoder_request));
}
