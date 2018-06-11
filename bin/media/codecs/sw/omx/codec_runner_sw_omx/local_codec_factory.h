// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_OMX_CODEC_RUNNER_SW_OMX_LOCAL_CODEC_FACTORY_H_
#define GARNET_BIN_MEDIA_CODECS_SW_OMX_CODEC_RUNNER_SW_OMX_LOCAL_CODEC_FACTORY_H_

#include <fuchsia/mediacodec/cpp/fidl.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fsl/tasks/message_loop.h"

#include <functional>

// The LocalCodecFactory implements CodecFactory, but it's a very limited
// implementation that isn't expected to ever grow into a full-fledged
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
// creation request.  Instead, this class only needs to deal with the latest
// parameter set, as the main CodecFactory will have already converted any older
// parameter set to the latest parameter set.  At the moment there's only one
// parameter set, but it's still important to avoid this class becoming more
// than was really intended.
//
// Any given instance of this class is only capable of creating the codec type
// for which it was instantiated, based on which constructor was called.  This
// de-fans the CodecFactory interface for the owning code.  It's fairly
// mechanical which is why it's a separate class to deal with the de-fan without
// really applying any real strategy in this class.
//
// TODO: We could consider using a different interface for this which only ever
// knows how to convey the latest parameter set, maybe all at once, but ... to
// some degree that seems like a pointless re-stating of part of the
// CodecFactory interface.  In any case, the interaction between the main
// CodecFactory and built-in SW codec isolates is something that only needs to
// handle the same build version on both sides.

namespace media_codec {

class CodecRunner;

class LocalCodecFactory : public CodecFactory {
 public:
  using BindAudioDecoderCallback = std::function<void(
      fidl::InterfaceRequest<Codec>, CreateAudioDecoder_Params audio_params)>;

  // This creates a self-owned CodecFactory instance that knows how to create
  // any of the codecs supported by this isolate process, regardless of which
  // codec type.
  static void CreateSelfOwned(async_t* fidl_async, thrd_t fidl_thread,
                              fidl::InterfaceRequest<CodecFactory> request);

  virtual void CreateAudioDecoder_Begin_Params(
      CreateAudioDecoder_Params audio_decoder_params_1) override;
  virtual void CreateAudioDecoder_Go(
      ::fidl::InterfaceRequest<Codec> audio_decoder) override;

  // TODO: Implement interface methods for:
  // audio encoder
  // video decoder
  // video encoder

 private:
  enum CodecType {
    kCodecTypeUnknown,
    kCodecTypeAudioDecoder,
    kCodecTypeVideoDecoder,
    kCodecTypeAudioEncoder,
    kCodecTypeVideoEncoder,
  };

  // We let CreateSelfOwned() deal with setting up the binding_ directly, which
  // means the constructor doesn't need to stash the
  // InterfaceRequest<CodecFactory>
  LocalCodecFactory(async_t* fidl_async, thrd_t fidl_thread);

  void Common_Begin(CodecType codec_type);
  void Common_Go(
      ::fidl::InterfaceRequest<Codec> codec_request,
      CodecType expected_codec_type, std::string mime_type,
      std::function<void(CodecRunner* codec_runner)> set_type_specific_params);

  // Some combinations of mime type and codec lib need a wrapper to compensate
  // for the OMX lib's behavior - to ensure that the overall Codec served by
  // this process conforms to the Codec interface rules.  For now this is
  // primarily about the OMX AAC decoder lib not dealing with split ADTS
  // headers, which the Codec interface requires.
  struct CodecStrategy {
    CodecType codec_type;
    std::string_view mime_type;
    std::string_view lib_filename;
    std::function<std::unique_ptr<CodecRunner>(
        async_t* async, thrd_t fidl_thread,
        const CodecStrategy& codec_strategy)>
        create_runner;
  };

  // Appropriate for use with any mime_type where the raw OMX codec doesn't have
  // any known open issues.
  static std::unique_ptr<CodecRunner> CreateRawOmxRunner(
      async_t* fidl_async, thrd_t fidl_thread,
      const CodecStrategy& codec_strategy);

  static std::unique_ptr<CodecRunner> CreateCodec(async_t* fidl_async,
                                                  thrd_t fidl_thread,
                                                  CodecType codec_type,
                                                  std::string mime_type);

  async_t* fidl_async_;
  thrd_t fidl_thread_ = 0;

  // The LocalCodecFactory instance is self-owned via binding_:
  typedef fidl::Binding<CodecFactory, std::unique_ptr<LocalCodecFactory>>
      BindingType;
  std::unique_ptr<BindingType> binding_;

  CodecType codec_type_;

  std::unique_ptr<CreateAudioDecoder_Params> audio_decoder_params_;
  // TODO: CreateAudioEncoder_Params1 audio_encoder_params_;
  // TODO: CreateVideoDecoder_Params1 video_decoder_params_;
  // TODO: CreateVideoEncoder_Params1 video_encoder_params_;

  static CodecStrategy codec_strategies[];
};

}  // namespace media_codec

#endif  // GARNET_BIN_MEDIA_CODECS_SW_OMX_CODEC_RUNNER_SW_OMX_LOCAL_CODEC_FACTORY_H_
