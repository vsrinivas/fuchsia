// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_ENCODER_LOCAL_CODEC_FACTORY_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_ENCODER_LOCAL_CODEC_FACTORY_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>
#include <lib/media/codec_impl/codec_adapter.h>
#include <lib/media/codec_impl/codec_admission_control.h>
#include <lib/media/codec_impl/codec_impl.h>

#include <fbl/macros.h>
class DeviceCtx;

class LocalCodecFactory : public fuchsia::mediacodec::CodecFactory {
 public:
  // device - parent device.
  LocalCodecFactory(
      async_dispatcher_t* fidl_dispatcher, DeviceCtx* device,
      fidl::InterfaceRequest<CodecFactory> request,
      fit::function<void(LocalCodecFactory*, std::unique_ptr<CodecImpl>)> factory_done_callback,
      CodecAdmissionControl* codec_admission_control,
      fit::function<void(LocalCodecFactory*, zx_status_t)> error_handler);

  ~LocalCodecFactory() override {}

  //
  // CodecFactory interface
  //

  void CreateDecoder(
      fuchsia::mediacodec::CreateDecoder_Params video_decoder_params,
      ::fidl::InterfaceRequest<fuchsia::media::StreamProcessor> video_decoder) override;

  void CreateEncoder(
      fuchsia::mediacodec::CreateEncoder_Params encoder_params,
      ::fidl::InterfaceRequest<fuchsia::media::StreamProcessor> encoder_request) override;

 private:
  async_dispatcher_t* fidl_dispatcher_;
  DeviceCtx* device_ = nullptr;
  fidl::Binding<fuchsia::mediacodec::CodecFactory, LocalCodecFactory*> binding_;
  // Returns the codec implementation and assumes drop of self.
  fit::function<void(LocalCodecFactory*, std::unique_ptr<CodecImpl>)> factory_done_callback_;
  // Assumes drop of self
  fit::function<void(LocalCodecFactory*, zx_status_t)> error_handler_;

  CodecAdmissionControl* codec_admission_control_;
};

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_ENCODER_LOCAL_CODEC_FACTORY_H_
