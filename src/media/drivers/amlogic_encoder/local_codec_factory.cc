// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/drivers/amlogic_encoder/local_codec_factory.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/fit/function.h>
#include <lib/media/codec_impl/codec_admission_control.h>

#include <optional>

#include "src/media/drivers/amlogic_encoder/codec_adapter_h264.h"
#include "src/media/drivers/amlogic_encoder/device_ctx.h"

namespace {

struct CodecAdapterFactory {
  fuchsia::mediacodec::CodecDescription description;
  fit::function<std::unique_ptr<CodecAdapter>(std::mutex& lock, CodecAdapterEvents*, DeviceCtx*)>
      create;
};

const CodecAdapterFactory kCodecFactories[] = {
    {
        fuchsia::mediacodec::CodecDescription{
            .codec_type = fuchsia::mediacodec::CodecType::ENCODER,
            .mime_type = "video/h264",

            .can_stream_bytes_input = false,
            .can_find_start = false,
            .can_re_sync = false,
            .will_report_all_detected_errors = false,
            .is_hw = true,
            .split_header_handling = false,
        },
        [](std::mutex& lock, CodecAdapterEvents* events, DeviceCtx* device) {
          return std::make_unique<CodecAdapterH264>(lock, events, device);
        },
    },
};

}  // namespace

LocalCodecFactory::LocalCodecFactory(
    async_dispatcher_t* fidl_dispatcher, DeviceCtx* device,
    fidl::InterfaceRequest<CodecFactory> request,
    fit::function<void(LocalCodecFactory*, std::unique_ptr<CodecImpl>)> factory_done_callback,
    CodecAdmissionControl* codec_admission_control,
    fit::function<void(LocalCodecFactory*, zx_status_t)> error_handler)
    : fidl_dispatcher_(fidl_dispatcher),
      device_(device),
      binding_(this),
      factory_done_callback_(std::move(factory_done_callback)),
      error_handler_(std::move(error_handler)),
      codec_admission_control_(codec_admission_control) {
  binding_.set_error_handler([this](zx_status_t status) {
    if (error_handler_) {
      error_handler_(this, status);
    }
  });

  zx_status_t status = binding_.Bind(std::move(request), fidl_dispatcher);
  ZX_ASSERT(status == ZX_OK);

  // All HW-accelerated local CodecFactory(s) must send OnCodecList()
  // immediately upon creation of the local CodecFactory.
  std::vector<fuchsia::mediacodec::CodecDescription> codec_descriptions;
  for (const CodecAdapterFactory& factory : kCodecFactories) {
    codec_descriptions.push_back(fidl::Clone(factory.description));
  }
  binding_.events().OnCodecList(std::move(codec_descriptions));
}

void LocalCodecFactory::CreateDecoder(
    fuchsia::mediacodec::CreateDecoder_Params video_decoder_params,
    ::fidl::InterfaceRequest<fuchsia::media::StreamProcessor> video_decoder) {
  // no decoder support here
}

void LocalCodecFactory::CreateEncoder(
    fuchsia::mediacodec::CreateEncoder_Params encoder_params,
    ::fidl::InterfaceRequest<fuchsia::media::StreamProcessor> encoder_request) {
  // Ignore channel errors (e.g. PEER_CLOSED) after this point, because this channel has served
  // its purpose. Otherwise the error handler could tear down the loop before the codec was
  // finished being added.
  binding_.set_error_handler([](auto) {});

  if (!encoder_params.has_input_details()) {
    return;
  }

  // TODO(afoxley) CreateEncoder_Params needs to be extended to provide desired output details.
  // For now, use our only defined codec adapter
  const CodecAdapterFactory* factory = &kCodecFactories[0];

  codec_admission_control_->TryAddCodec(
      /*multi_instance=*/false, [this, factory, encoder_params = std::move(encoder_params),
                                 encoder_request = std::move(encoder_request)](
                                    std::unique_ptr<CodecAdmission> codec_admission) mutable {
        if (!codec_admission) {
          // ~encoder_request closes channel.
          return;
        }

        fidl::InterfaceHandle<fuchsia::sysmem::Allocator> sysmem = device_->ConnectToSysmem();
        if (!sysmem) {
          return;
        }

        auto codec_impl = std::make_unique<CodecImpl>(
            std::move(sysmem), std::move(codec_admission), fidl_dispatcher_, thrd_current(),
            std::move(encoder_params), std::move(encoder_request));

        codec_impl->SetCoreCodecAdapter(
            factory->create(codec_impl->lock(), codec_impl.get(), device_));

        // This hands off the codec impl to the creator of |this| and is
        // expected to |~this|.
        factory_done_callback_(this, std::move(codec_impl));
      });
}
