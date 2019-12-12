// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_CODECS_SW_LOCAL_SINGLE_CODEC_FACTORY_H_
#define SRC_MEDIA_CODEC_CODECS_SW_LOCAL_SINGLE_CODEC_FACTORY_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/media/codec_impl/codec_adapter.h>
#include <lib/media/codec_impl/codec_admission_control.h>
#include <lib/media/codec_impl/codec_impl.h>
#include <threads.h>

#include "src/lib/syslog/cpp/logger.h"

// Marker type to specify these is no adapter to serve a request.
class NoAdapter {};

// Prepares a single codec for the codec runner and then requests drop of self.
// If a software can only provide an encoder or decoder, the other should be
// assigned NoAdapter in the template arguments, e.g.:
//   LocalSingleCodecFactory<CodecAdapterFfmpeg, NoAdapter>
template <typename DecoderAdapter, typename EncoderAdapter>
class LocalSingleCodecFactory : public fuchsia::mediacodec::CodecFactory {
 public:
  LocalSingleCodecFactory(async_dispatcher_t* fidl_dispatcher,
                          fidl::InterfaceHandle<fuchsia::sysmem::Allocator> sysmem,
                          fidl::InterfaceRequest<CodecFactory> request,
                          fit::function<void(std::unique_ptr<CodecImpl>)> factory_done_callback,
                          CodecAdmissionControl* codec_admission_control,
                          fit::function<void(zx_status_t)> error_handler)
      : fidl_dispatcher_(fidl_dispatcher),
        sysmem_(std::move(sysmem)),
        binding_(this),
        factory_done_callback_(std::move(factory_done_callback)),
        codec_admission_control_(codec_admission_control) {
    binding_.set_error_handler(std::move(error_handler));
    zx_status_t status = binding_.Bind(std::move(request), fidl_dispatcher);
    ZX_ASSERT(status == ZX_OK);
  }

  virtual void CreateDecoder(
      fuchsia::mediacodec::CreateDecoder_Params decoder_params,
      fidl::InterfaceRequest<fuchsia::media::StreamProcessor> decoder_request) override {
    VendCodecAdapter<DecoderAdapter>(std::move(decoder_params), std::move(decoder_request));
  }

  virtual void CreateEncoder(
      fuchsia::mediacodec::CreateEncoder_Params encoder_params,
      fidl::InterfaceRequest<fuchsia::media::StreamProcessor> encoder_request) override {
    VendCodecAdapter<EncoderAdapter>(std::move(encoder_params), std::move(encoder_request));
  }

 private:
  template <typename Adapter, typename Params>
  void VendCodecAdapter(Params params,
                        fidl::InterfaceRequest<fuchsia::media::StreamProcessor> codec_request) {
    // Ignore channel errors (e.g. PEER_CLOSED) after this point, because this channel has served
    // its purpose. Otherwise the error handler could tear down the loop before the codec was
    // finished being added.
    binding_.set_error_handler([](auto) {});
    codec_admission_control_->TryAddCodec(
        /*multi_instance=*/true,
        [this, params = std::move(params), codec_request = std::move(codec_request)](
            std::unique_ptr<CodecAdmission> codec_admission) mutable {
          if (!codec_admission) {
            // ~codec_request closes channel.
            return;
          }

          if (!sysmem_) {
            FX_LOGS(WARNING) << "VendCodecAdapter() only meant to be used once per "
                                "LocalSingleCodecFactory\n";
            // ~codec_request closes channel.
            return;
          }

          auto codec_impl = std::make_unique<CodecImpl>(
              std::move(sysmem_), std::move(codec_admission), fidl_dispatcher_, thrd_current(),
              std::move(params), std::move(codec_request));

          codec_impl->SetCoreCodecAdapter(
              std::make_unique<Adapter>(codec_impl->lock(), codec_impl.get()));

          // This hands off the codec impl to the creator of |this| and is
          // expected to |~this|.
          factory_done_callback_(std::move(codec_impl));
        });
  }

  template <>
  void VendCodecAdapter<NoAdapter, fuchsia::mediacodec::CreateDecoder_Params>(
      fuchsia::mediacodec::CreateDecoder_Params params,
      fidl::InterfaceRequest<fuchsia::media::StreamProcessor> codec_request) {
    // No adapter.
    // ~codec_request.
  }

  template <>
  void VendCodecAdapter<NoAdapter, fuchsia::mediacodec::CreateEncoder_Params>(
      fuchsia::mediacodec::CreateEncoder_Params params,
      fidl::InterfaceRequest<fuchsia::media::StreamProcessor> codec_request) {
    // No adapter.
    // ~codec_request.
  }

  async_dispatcher_t* fidl_dispatcher_;
  fidl::InterfaceHandle<fuchsia::sysmem::Allocator> sysmem_;
  fidl::Binding<CodecFactory, LocalSingleCodecFactory*> binding_;
  // Returns the codec implementation and requests drop of self.
  fit::function<void(std::unique_ptr<CodecImpl>)> factory_done_callback_;
  CodecAdmissionControl* codec_admission_control_;
};

#endif  // SRC_MEDIA_CODEC_CODECS_SW_LOCAL_SINGLE_CODEC_FACTORY_H_
