// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/src/framework/models/transform.h"
#include "apps/media/src/framework/packet.h"
#include "apps/media/src/framework/payload_allocator.h"
#include "apps/media/src/framework/result.h"
#include "apps/media/src/framework/types/stream_type.h"

namespace mojo {
namespace media {

// Abstract base class for transforms that decode compressed media.
class Decoder : public Transform {
 public:
  // Creates a Decoder object for a given stream type.
  static Result Create(const StreamType& stream_type,
                       std::shared_ptr<Decoder>* decoder_out);

  ~Decoder() override {}

  // Returns the type of the stream the decoder will produce.
  virtual std::unique_ptr<StreamType> output_stream_type() = 0;
};

}  // namespace media
}  // namespace mojo
