// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "local_single_codec_factory.h"

#include <lib/component/cpp/startup_context.h>

#include "codec_adapter_ffmpeg_decoder.h"

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
    ::fidl::InterfaceRequest<fuchsia::mediacodec::Codec> decoder_request) {
  codec_admission_control_->TryAddCodec(
      /*multi_instance=*/false,
      [this, decoder_params = std::move(decoder_params),
       decoder_request = std::move(decoder_request)](
          std::unique_ptr<CodecAdmission> codec_admission) mutable {
        if (!codec_admission) {
          // ~decoder_request closes channel.
          return;
        }

        auto codec_impl = std::make_unique<CodecImpl>(
            std::move(codec_admission), fidl_dispatcher_, thrd_current(),
            std::make_unique<fuchsia::mediacodec::CreateDecoder_Params>(
                std::move(decoder_params)),
            std::move(decoder_request));

        codec_impl->SetCoreCodecAdapter(
            std::make_unique<CodecAdapterFfmpegDecoder>(codec_impl->lock(),
                                                        codec_impl.get()));

        // This hands off the codec impl to the creator of |this| and is
        // expected to |~this|.
        factory_done_callback_(std::move(codec_impl));
      });
}