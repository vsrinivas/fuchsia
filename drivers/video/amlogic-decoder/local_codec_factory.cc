// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "local_codec_factory.h"

#include "device_ctx.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/fxl/arraysize.h>
#include <lib/fxl/logging.h>

namespace {

// TODO(dustingreen): Fix up this list to correspond to what
// CodecImpl+AmlogicVideo can actaully handle so far, once there's at least one
// format in that list.  For now this list is here to allow covering some
// LocalCodecFactory code.
const fuchsia::mediacodec::CodecDescription kCodecs[] = {
    fuchsia::mediacodec::CodecDescription{
        .codec_type = fuchsia::mediacodec::CodecType::DECODER,
        // TODO(dustingreen): See TODO comments on this field in
        // codec_common.fidl.
        .mime_type = "video/h264",

        // TODO(dustingreen): Determine which of these can safely indicate more
        // capability.
        .can_stream_bytes_input = false,
        .can_find_start = false,
        .can_re_sync = false,
        .will_report_all_detected_errors = false,

        .is_hw = true,

        // TODO(dustingreen): Determine if this claim of "true" is actually the
        // case.
        .split_header_handling = true,
    },
    fuchsia::mediacodec::CodecDescription{
        .codec_type = fuchsia::mediacodec::CodecType::DECODER,
        // TODO(dustingreen): See TODO comments on this field in
        // codec_common.fidl.
        .mime_type = "video/mpeg2",

        // TODO(dustingreen): Determine which of these can safely indicate more
        // capability.
        .can_stream_bytes_input = false,
        .can_find_start = false,
        .can_re_sync = false,
        .will_report_all_detected_errors = false,

        .is_hw = true,

        // TODO(dustingreen): Determine if this claim of "true" is actually the
        // case.
        .split_header_handling = true,
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
  // to un-bind unilaterally.  Unless not bound in the first place.
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
  fidl::VectorPtr<fuchsia::mediacodec::CodecDescription> codecs;
  for (size_t i = 0; i < arraysize(kCodecs); i++) {
    codecs.push_back(fidl::Clone(kCodecs[i]));
  }
  factory_binding_.events().OnCodecList(std::move(codecs));
}

void LocalCodecFactory::CreateDecoder(
    fuchsia::mediacodec::CreateDecoder_Params video_decoder_params,
    ::fidl::InterfaceRequest<fuchsia::mediacodec::Codec> video_decoder) {
  std::unique_ptr<CodecImpl> codec = std::make_unique<CodecImpl>(
      device_, std::move(video_decoder_params), std::move(video_decoder));
  device_->device_fidl()->BindCodecImpl(std::move(codec));
}
