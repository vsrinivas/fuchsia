// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "local_codec_factory.h"

#include "device_ctx.h"

#include <lib/fxl/logging.h>

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
                        device_->driver()->shared_fidl_loop()->async());
}

void LocalCodecFactory::CreateDecoder(
    fuchsia::mediacodec::CreateDecoder_Params video_decoder_params,
    ::fidl::InterfaceRequest<fuchsia::mediacodec::Codec> video_decoder) {
  std::unique_ptr<CodecImpl> codec = std::make_unique<CodecImpl>(
      device_, std::move(video_decoder_params), std::move(video_decoder));
  device_->device_fidl()->BindCodecImpl(std::move(codec));
}
