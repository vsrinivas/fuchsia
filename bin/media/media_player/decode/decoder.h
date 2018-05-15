// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_DECODE_DECODER_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_DECODE_DECODER_H_

#include "garnet/bin/media/media_player/framework/models/async_node.h"
#include "garnet/bin/media/media_player/framework/packet.h"
#include "garnet/bin/media/media_player/framework/payload_allocator.h"
#include "garnet/bin/media/media_player/framework/result.h"
#include "garnet/bin/media/media_player/framework/types/stream_type.h"

namespace media_player {

// Abstract base class for transforms that decode compressed media.
class Decoder : public AsyncNode {
 public:
  // Creates a Decoder object for a given stream type.
  static Result Create(const StreamType& stream_type,
                       std::shared_ptr<Decoder>* decoder_out);

  ~Decoder() override {}

  // Returns the type of the stream the decoder will produce.
  virtual std::unique_ptr<StreamType> output_stream_type() const = 0;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_DECODE_DECODER_H_
