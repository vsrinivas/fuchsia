// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "local_codec_factory.h"

#include "device_ctx.h"

#include "codec_adapter_h264.h"
#include "codec_adapter_mpeg2.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/fxl/logging.h>

namespace {

struct CodecAdapterFactory {
  fuchsia::mediacodec::CodecDescription description;

  // This typedef is just for local readability here, not for use outside this
  // struct.
  using CreateFunction = std::function<std::unique_ptr<CodecAdapter>(
      std::mutex& lock, CodecAdapterEvents*, AmlogicVideo*)>;

  CreateFunction create;
};

// TODO(dustingreen): Fix up this list to correspond to what
// CodecImpl+AmlogicVideo can actaully handle so far, once there's at least one
// format in that list.  For now this list is here to allow covering some
// LocalCodecFactory code.
const CodecAdapterFactory kCodecFactories[] = {
    {
        fuchsia::mediacodec::CodecDescription{
            .codec_type = fuchsia::mediacodec::CodecType::DECODER,
            // TODO(dustingreen): See TODO comments on this field in
            // codec_common.fidl.
            .mime_type = "video/h264",

            // TODO(dustingreen): Determine which of these can safely indicate
            // more capability.
            .can_stream_bytes_input = false,
            .can_find_start = false,
            .can_re_sync = false,
            .will_report_all_detected_errors = false,

            .is_hw = true,

            // TODO(dustingreen): Determine if this claim of "true" is actually
            // the case.
            .split_header_handling = true,
        },
        [](std::mutex& lock, CodecAdapterEvents* events, AmlogicVideo* video) {
          return std::make_unique<CodecAdapterH264>(lock, events, video);
        },
    },
    {
        fuchsia::mediacodec::CodecDescription{
            .codec_type = fuchsia::mediacodec::CodecType::DECODER,
            // TODO(dustingreen): See TODO comments on this field in
            // codec_common.fidl.
            .mime_type = "video/mpeg2",

            // TODO(dustingreen): Determine which of these can safely indicate
            // more capability.
            .can_stream_bytes_input = false,
            .can_find_start = false,
            .can_re_sync = false,
            .will_report_all_detected_errors = false,

            .is_hw = true,

            // TODO(dustingreen): Determine if this claim of "true" is actually
            // the case.
            .split_header_handling = true,
        },
        [](std::mutex& lock, CodecAdapterEvents* events, AmlogicVideo* video) {
          return std::make_unique<CodecAdapterMpeg2>(lock, events, video);
        },
    },
};

}  // namespace

// device - associated device.
LocalCodecFactory::LocalCodecFactory(DeviceCtx* device)
    : device_(device), factory_binding_(this) {
  // nothing else to do here
}

LocalCodecFactory::~LocalCodecFactory() {
  // We need ~factory_binding_ to run on shared_fidl_thread() else it's not safe
  // to un-bind unilaterally (without the channel closing).  Unless not bound in
  // the first place.
  FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread() ||
             !factory_binding_.is_bound());

  // ~factory_binding_ here + fact that we're running on shared_fidl_thread()
  // (if Bind() previously called) means error_handler won't be running
  // concurrently with ~LocalCodecFactory and won't run after ~factory_binding_
  // here.
}

void LocalCodecFactory::SetErrorHandler(fit::closure error_handler) {
  FXL_DCHECK(!factory_binding_.is_bound());
  factory_binding_.set_error_handler(
      [this, error_handler = std::move(error_handler)] {
        FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread());
        error_handler();
      });
  is_error_handler_set_ = true;
}

void LocalCodecFactory::Bind(zx::channel server_endpoint) {
  FXL_DCHECK(is_error_handler_set_);
  FXL_DCHECK(!factory_binding_.is_bound());

  // Go!  (immediately - if Bind() is called on IOCTL thread, this can result in
  // _immediate_ dispatching over on shared_fidl_thread()).
  factory_binding_.Bind(std::move(server_endpoint),
                        device_->driver()->shared_fidl_loop()->dispatcher());

  // All HW-accelerated local CodecFactory(s) must send OnCodecList()
  // immediately upon creation of the local CodecFactory.
  fidl::VectorPtr<fuchsia::mediacodec::CodecDescription> codec_descriptions;
  for (const CodecAdapterFactory& factory : kCodecFactories) {
    codec_descriptions.push_back(fidl::Clone(factory.description));
  }
  factory_binding_.events().OnCodecList(std::move(codec_descriptions));
}

void LocalCodecFactory::CreateDecoder(
    fuchsia::mediacodec::CreateDecoder_Params video_decoder_params,
    ::fidl::InterfaceRequest<fuchsia::mediacodec::Codec> video_decoder) {
  const CodecAdapterFactory* factory = nullptr;
  for (const CodecAdapterFactory& candidate_factory : kCodecFactories) {
    if (candidate_factory.description.mime_type ==
        video_decoder_params.input_details.mime_type) {
      factory = &candidate_factory;
      break;
    }
  }
  if (!factory) {
    // This shouldn't really happen since the main CodecFactory shouldn't be
    // asking this LocalCodecFactory for a codec fitting a description that's
    // not a description this factory previously delivered to the main
    // CodecFactory via OnCodecList().
    //
    // TODO(dustingreen): epitaph for video_decoder.
    //
    // ~video_decoder here will take care of closing the channel
    return;
  }

  std::unique_ptr<CodecImpl> codec = std::make_unique<CodecImpl>(
      device_,
      std::make_unique<fuchsia::mediacodec::CreateDecoder_Params>(
          std::move(video_decoder_params)),
      std::move(video_decoder));

  codec->SetCoreCodecAdapter(
      factory->create(codec->lock(), codec.get(), device_->video()));

  device_->device_fidl()->BindCodecImpl(std::move(codec));
}
