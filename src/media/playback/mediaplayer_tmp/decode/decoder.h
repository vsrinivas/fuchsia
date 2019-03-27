// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_DECODE_DECODER_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_DECODE_DECODER_H_

#include "lib/component/cpp/startup_context.h"
#include "src/media/playback/mediaplayer_tmp/graph/nodes/node.h"
#include "src/media/playback/mediaplayer_tmp/graph/packet.h"
#include "src/media/playback/mediaplayer_tmp/graph/payloads/payload_allocator.h"
#include "src/media/playback/mediaplayer_tmp/graph/result.h"
#include "src/media/playback/mediaplayer_tmp/graph/types/stream_type.h"

namespace media_player {

// Abstract base class for nodes that decode compressed media.
class Decoder : public Node {
 public:
  ~Decoder() override {}

  // Returns the type of the stream the decoder will produce.
  virtual std::unique_ptr<StreamType> output_stream_type() const = 0;
};

// Abstract base class for |Decoder| factories.
class DecoderFactory {
 public:
  // Creates a decoder factory.
  static std::unique_ptr<DecoderFactory> Create(
      component::StartupContext* startup_context);

  virtual ~DecoderFactory() {}

  // Creates a |Decoder| object for a given stream type. Calls back with a
  // decoder if the operation succeeds, with nullptr if not. This method may
  // call back synchronously.
  virtual void CreateDecoder(
      const StreamType& stream_type,
      fit::function<void(std::shared_ptr<Decoder>)> callback) = 0;

 protected:
  DecoderFactory() {}

 private:
  // Disallow copy and assign.
  DecoderFactory(const DecoderFactory&) = delete;
  DecoderFactory& operator=(const DecoderFactory&) = delete;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_DECODE_DECODER_H_
