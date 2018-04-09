// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_DECODE_DECODER_H_
#define GARNET_BIN_MEDIA_DECODE_DECODER_H_

#include "garnet/bin/media/framework/models/transform.h"
#include "garnet/bin/media/framework/packet.h"
#include "garnet/bin/media/framework/payload_allocator.h"
#include "garnet/bin/media/framework/result.h"
#include "garnet/bin/media/framework/types/stream_type.h"

namespace media_player {

// Abstract base class for transforms that decode compressed media.
class Decoder : public Transform {
 public:
  // Creates a Decoder object for a given stream type.
  static media::Result Create(const media::StreamType& stream_type,
                              std::shared_ptr<Decoder>* decoder_out);

  ~Decoder() override {}

  // Returns the type of the stream the decoder will produce.
  virtual std::unique_ptr<media::StreamType> output_stream_type() = 0;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_DECODE_DECODER_H_
