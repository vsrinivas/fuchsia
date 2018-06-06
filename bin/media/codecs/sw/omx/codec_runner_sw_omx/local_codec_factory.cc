// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "local_codec_factory.h"

#include "codec_runner.h"
#include "omx_codec_runner.h"

#include <lib/fxl/arraysize.h>

#include <list>
#include <map>

using namespace media_codec;

namespace {

char kLibDecoderAac[] = "libcodec_sw_omx_dec_aac.so";

}  // namespace

LocalCodecFactory::CodecStrategy LocalCodecFactory::codec_strategies[] = {
    CodecStrategy{kCodecTypeAudioDecoder, "audio/aac", kLibDecoderAac,
                  CreateRawOmxRunner},
    // TODO: Instead of CreateRawOmxRunner, create a wrapper that deals with the
    // lack of kLibDecoderAac support for split ADTS headers, which so far is
    // unique to this mime type.  Until we get the rest working we'll just use
    // the CreateRawOmxRunner without any wrapper and avoid annoying the broken
    // Codec in the client code, but the Codec for this mime type should be made
    // to work correctly one way or another before too long.
    CodecStrategy{kCodecTypeAudioDecoder, "audio/aac-adts", kLibDecoderAac,
                  CreateRawOmxRunner},
};

void LocalCodecFactory::CreateSelfOwned(
    async_t* fidl_async, thrd_t fidl_thread,
    fidl::InterfaceRequest<CodecFactory> codec_factory_request) {
  std::unique_ptr<LocalCodecFactory> codec_factory(
      new LocalCodecFactory(fidl_async, fidl_thread));
  // C++ evaluation order is mostly arbitrary within a statement, so stash this
  // result of unique_ptr::operator->() to avoid moving the same unique_ptr in a
  // single statement.  The actual pointed-at instance isn't moving, so it's
  // fine to have this ref for a moment here.
  std::unique_ptr<BindingType>& binding = codec_factory->binding_;
  binding = std::make_unique<BindingType>(
      std::move(codec_factory), std::move(codec_factory_request), fidl_async);
}

LocalCodecFactory::LocalCodecFactory(async_t* fidl_async, thrd_t fidl_thread)
    : fidl_async_(fidl_async),
      fidl_thread_(fidl_thread),
      codec_type_(kCodecTypeUnknown) {
  // nothing else to do here
}

// AudioDecoder:

void LocalCodecFactory::CreateAudioDecoder_Begin_Params(
    CreateAudioDecoder_Params audio_decoder_params_1) {
  Common_Begin(kCodecTypeAudioDecoder);
  audio_decoder_params_ = std::make_unique<CreateAudioDecoder_Params>(
      std::move(audio_decoder_params_1));
}

void LocalCodecFactory::CreateAudioDecoder_Go(
    ::fidl::InterfaceRequest<Codec> audio_decoder_request) {
  Common_Go(
      std::move(audio_decoder_request), kCodecTypeAudioDecoder,
      audio_decoder_params_->input_details.mime_type,
      [this](CodecRunner* codec_runner) {
        assert(audio_decoder_params_);
        codec_runner->SetAudioDecoderParams(std::move(*audio_decoder_params_));
        audio_decoder_params_.reset();
      });
}

// TODO:
// AudioEncoder:
// VideoDecoder:
// VideoEncoder:

void LocalCodecFactory::Common_Begin(CodecType codec_type) {
  if (codec_type_ != kCodecTypeUnknown) {
    // TODO: epitaph, log
    binding_.reset();
    assert(codec_type_ == kCodecTypeUnknown);
    exit(-1);
  }
  codec_type_ = codec_type;
}

void LocalCodecFactory::Common_Go(
    ::fidl::InterfaceRequest<Codec> codec_request,
    CodecType expected_codec_type, std::string mime_type,
    std::function<void(CodecRunner* codec_runner)> set_type_specific_params) {
  if (codec_type_ != expected_codec_type) {
    // TODO: epitaph, log
    codec_request.TakeChannel();  // ~zx::channel
    binding_.reset();
    assert(codec_type_ == expected_codec_type);
    exit(-1);
  }
  std::unique_ptr<CodecRunner> codec_runner =
      CreateCodec(fidl_async_, fidl_thread_, codec_type_, mime_type);
  if (!codec_runner) {
    // TODO: epitaph, log
    codec_request.TakeChannel();  // ~zx::channel
    binding_.reset();
    assert(codec_runner);
    exit(-1);
  }
  set_type_specific_params(codec_runner.get());
  codec_runner->ComputeInputConstraints();
  CodecRunner& codec_runner_ref = *codec_runner;
  codec_runner_ref.BindAndOwnSelf(std::move(codec_request),
                                  std::move(codec_runner));
  // This CodecFactory instance is done creating the one Codec that this factory
  // is willing to create, and that one Codec is now self-owned (owned by its
  // own channel), so self-destruct "this" here:
  binding_.reset();
}

// Appropriate for use with any mime_type where the raw OMX codec doesn't have
// any known open issues.
//
// TODO(dustingreen): We're currently using this method for audio/aac-adts, but
// instead the OMX codec runner will need to extract its own
// make_AudioSpecificConfig_from_ADTS_header() data instead of relying on the
// client to pass it down.  TBD whether we use a wrapper for that or a more
// targetted behavior override.  Either this method needs to know or another
// method to create a different way needs to exist.
std::unique_ptr<CodecRunner> LocalCodecFactory::CreateRawOmxRunner(
    async_t* fidl_async, thrd_t fidl_thread,
    const CodecStrategy& codec_strategy) {
  return std::make_unique<OmxCodecRunner>(fidl_async, fidl_thread,
                                          codec_strategy.mime_type,
                                          codec_strategy.lib_filename);
}

std::unique_ptr<CodecRunner> LocalCodecFactory::CreateCodec(
    async_t* fidl_async, thrd_t fidl_thread, CodecType codec_type,
    std::string mime_type) {
  const CodecStrategy* strategy = nullptr;
  for (size_t i = 0; i < arraysize(codec_strategies); i++) {
    if (codec_strategies[i].codec_type == codec_type &&
        codec_strategies[i].mime_type == mime_type) {
      strategy = &codec_strategies[i];
      break;
    }
  }
  if (!strategy) {
    return nullptr;
  }
  std::unique_ptr<CodecRunner> codec_runner =
      strategy->create_runner(fidl_async, fidl_thread, *strategy);
  if (!codec_runner) {
    return nullptr;
  }
  if (!codec_runner->Load()) {
    return nullptr;
  }
  return codec_runner;
}
