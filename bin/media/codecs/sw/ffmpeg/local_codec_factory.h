// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_LOCAL_CODEC_FACTORY_H_
#define GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_LOCAL_CODEC_FACTORY_H_

#include <threads.h>

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

namespace codec_factory {

// TODO(turnage): Implement
class LocalCodecFactory : public fuchsia::mediacodec::CodecFactory {
 public:
  static void CreateSelfOwned(async_dispatcher_t* fidl_dispatcher);

  virtual void CreateDecoder(
      fuchsia::mediacodec::CreateDecoder_Params decoder_params,
      ::fidl::InterfaceRequest<fuchsia::mediacodec::Codec> decoder_request)
      override;

 private:
  LocalCodecFactory();

  std::unique_ptr<
      fidl::Binding<CodecFactory, std::unique_ptr<LocalCodecFactory>>>
      binding_;
};

}  // namespace codec_factory

#endif  // GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_LOCAL_CODEC_FACTORY_H_