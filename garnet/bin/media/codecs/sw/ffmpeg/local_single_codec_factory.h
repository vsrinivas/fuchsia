// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_LOCAL_SINGLE_CODEC_FACTORY_H_
#define GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_LOCAL_SINGLE_CODEC_FACTORY_H_

#include <threads.h>

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/media/codec_impl/codec_adapter.h>
#include <lib/media/codec_impl/codec_admission_control.h>
#include <lib/media/codec_impl/codec_impl.h>

// Prepares a single codec for the codec runner and then requests drop of self.
class LocalSingleCodecFactory : public fuchsia::mediacodec::CodecFactory {
 public:
  LocalSingleCodecFactory(
      async_dispatcher_t* fidl_dispatcher,
      fidl::InterfaceRequest<CodecFactory> request,
      fit::function<void(std::unique_ptr<CodecImpl>)> factory_done_callback,
      CodecAdmissionControl* codec_admission_control,
      fit::function<void(zx_status_t)> error_handler);

  virtual void CreateDecoder(
      fuchsia::mediacodec::CreateDecoder_Params decoder_params,
      ::fidl::InterfaceRequest<fuchsia::media::StreamProcessor> decoder_request)
      override;

  virtual void CreateEncoder(
      fuchsia::mediacodec::CreateEncoder_Params encoder_params,
      ::fidl::InterfaceRequest<fuchsia::media::StreamProcessor> encoder_request)
      override;

 private:
  template <typename Adapter, typename Params>
  void VendCodecAdapter(
      Params params,
      ::fidl::InterfaceRequest<fuchsia::media::StreamProcessor> codec_request) {
    codec_admission_control_->TryAddCodec(
        /*multi_instance=*/true,
        [this, params = std::move(params),
         codec_request = std::move(codec_request)](
            std::unique_ptr<CodecAdmission> codec_admission) mutable {
          if (!codec_admission) {
            // ~codec_request closes channel.
            return;
          }

          auto codec_impl = std::make_unique<CodecImpl>(
              std::move(codec_admission), fidl_dispatcher_, thrd_current(),
              std::make_unique<Params>(std::move(params)),
              std::move(codec_request));

          codec_impl->SetCoreCodecAdapter(
              std::make_unique<Adapter>(codec_impl->lock(), codec_impl.get()));

          // This hands off the codec impl to the creator of |this| and is
          // expected to |~this|.
          factory_done_callback_(std::move(codec_impl));
        });
  }

  async_dispatcher_t* fidl_dispatcher_;
  fidl::Binding<CodecFactory, LocalSingleCodecFactory*> binding_;
  // Returns the codec implementation and requests drop of self.
  fit::function<void(std::unique_ptr<CodecImpl>)> factory_done_callback_;
  CodecAdmissionControl* codec_admission_control_;
};

#endif  // GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_LOCAL_SINGLE_CODEC_FACTORY_H_
