// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_OMX_CODEC_RUNNER_SW_OMX_LOCAL_CODEC_FACTORY_H_
#define GARNET_BIN_MEDIA_CODECS_SW_OMX_CODEC_RUNNER_SW_OMX_LOCAL_CODEC_FACTORY_H_

#include <threads.h>

#include <functional>

#include <fuchsia/mediacodec/cpp/fidl.h>

#include "lib/fidl/cpp/binding.h"

// The LocalCodecFactory implements CodecFactory, but it's a very limited local
// implementation.  The main implementation of CodecFactory is in
// codec_codec_factory_impl.h/cc.
//
// The point of the implementation here is to do some basic sanity checks,
// accept config info, and call the owner of this class back to achieve the
// actual binding of the server end of a Codec channel to a Codec implementation
// provided by the owner.  That way the owner can wire up the details however
// the owner wants.
//
// This class does not need to deal with every potential version of a codec
// creation request.  Instead, this class only needs to deal with requests made
// by the latest main CodecFactory implementation, as the main CodecFactory will
// have already converted any older-style requests to the latest style.
//
// Any given instance of this class is only capable of creating the codec type
// for which it was instantiated, based on which constructor was called.  This
// de-fans the CodecFactory interface for the owning code.  It's fairly
// mechanical which is why it's a separate class to deal with the de-fan without
// really applying any real strategy in this class.
//
// The interaction between the main CodecFactory and built-in SW codec isolates
// is something that only needs to handle the same build version on both sides.

namespace codec_runner {
class CodecRunner;
}  // namespace codec_runner

namespace codec_factory {

class LocalCodecFactory : public fuchsia::mediacodec::CodecFactory {
 public:
  using BindAudioDecoderCallback = std::function<void(
      fidl::InterfaceRequest<fuchsia::mediacodec::Codec>,
      fuchsia::mediacodec::CreateDecoder_Params audio_params)>;

  // This creates a self-owned CodecFactory instance that knows how to create
  // any of the codecs supported by this isolate process, regardless of which
  // codec type.
  static void CreateSelfOwned(
      async_dispatcher_t* fidl_dispatcher, thrd_t fidl_thread,
      fidl::InterfaceRequest<fuchsia::mediacodec::CodecFactory> request);

  virtual void CreateDecoder(
      fuchsia::mediacodec::CreateDecoder_Params audio_decoder_params,
      ::fidl::InterfaceRequest<fuchsia::mediacodec::Codec> audio_decoder)
      override;

  // TODO(dustingreen): Implement interface methods for:
  // audio encoder
  // video encoder
  // (or combined)

 private:
  // We let CreateSelfOwned() deal with setting up the binding_ directly, which
  // means the constructor doesn't need to stash the
  // InterfaceRequest<CodecFactory>
  LocalCodecFactory(async_dispatcher_t* fidl_dispatcher, thrd_t fidl_thread);

  void CreateCommon(
      ::fidl::InterfaceRequest<fuchsia::mediacodec::Codec> codec_request,
      fuchsia::mediacodec::CodecType codec_type, std::string mime_type,
      fit::function<void(codec_runner::CodecRunner* codec_runner)>
          set_type_specific_params);

  // Some combinations of mime type and codec lib need a wrapper to compensate
  // for the OMX lib's behavior - to ensure that the overall Codec served by
  // this process conforms to the Codec interface rules.  For now this is
  // primarily about the OMX AAC decoder lib not dealing with split ADTS
  // headers, which the Codec interface requires.
  struct CodecStrategy {
    fuchsia::mediacodec::CodecType codec_type;
    std::string_view mime_type;
    std::string_view lib_filename;
    std::function<std::unique_ptr<codec_runner::CodecRunner>(
        async_dispatcher_t* dispatcher, thrd_t fidl_thread,
        const CodecStrategy& codec_strategy)>
        create_runner;
  };

  // Appropriate for use with any mime_type where the raw OMX codec doesn't have
  // any known open issues.
  static std::unique_ptr<codec_runner::CodecRunner> CreateRawOmxRunner(
      async_dispatcher_t* fidl_dispatcher, thrd_t fidl_thread,
      const CodecStrategy& codec_strategy);

  static std::unique_ptr<codec_runner::CodecRunner> CreateCodec(
      async_dispatcher_t* fidl_dispatcher, thrd_t fidl_thread,
      fuchsia::mediacodec::CodecType codec_type, std::string mime_type);

  async_dispatcher_t* fidl_dispatcher_;
  thrd_t fidl_thread_ = 0;

  // The LocalCodecFactory instance is self-owned via binding_:
  typedef fidl::Binding<CodecFactory, std::unique_ptr<LocalCodecFactory>>
      BindingType;
  std::unique_ptr<BindingType> binding_;

  static CodecStrategy codec_strategies[];
};

}  // namespace codec_factory

#endif  // GARNET_BIN_MEDIA_CODECS_SW_OMX_CODEC_RUNNER_SW_OMX_LOCAL_CODEC_FACTORY_H_
