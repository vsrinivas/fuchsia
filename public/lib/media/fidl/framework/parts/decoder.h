// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_FRAMEWORK_PARTS_DECODER_H_
#define SERVICES_MEDIA_FRAMEWORK_PARTS_DECODER_H_

#include "services/media/framework/models/transform.h"
#include "services/media/framework/packet.h"
#include "services/media/framework/payload_allocator.h"
#include "services/media/framework/result.h"
#include "services/media/framework/types/stream_type.h"

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

#endif  // SERVICES_MEDIA_FRAMEWORK_PARTS_DECODER_H_
