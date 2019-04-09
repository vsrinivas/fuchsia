// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FIDL_FIDL_DECODER_FACTORY_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FIDL_FIDL_DECODER_FACTORY_H_

#include <fuchsia/mediacodec/cpp/fidl.h>

#include <memory>

#include "src/media/playback/mediaplayer/decode/decoder.h"

namespace media_player {

// Factory for fidl decoders.
class FidlDecoderFactory : public DecoderFactory {
 public:
  // Creates a fidl decoder factory.
  static std::unique_ptr<DecoderFactory> Create(
      component::StartupContext* startup_context);

  FidlDecoderFactory(component::StartupContext* startup_context);

  ~FidlDecoderFactory() override;

  void CreateDecoder(
      const StreamType& stream_type,
      fit::function<void(std::shared_ptr<Decoder>)> callback) override;

 private:
  fuchsia::mediacodec::CodecFactoryPtr codec_factory_;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FIDL_FIDL_DECODER_FACTORY_H_
