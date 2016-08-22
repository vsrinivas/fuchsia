// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_FRAMEWORK_MODELS_MULTISTREAM_SOURCE_H_
#define SERVICES_MEDIA_FRAMEWORK_MODELS_MULTISTREAM_SOURCE_H_

#include "services/media/framework/models/part.h"
#include "services/media/framework/packet.h"

namespace mojo {
namespace media {

// Synchronous source of packets for multiple streams.
class MultistreamSource : public Part {
 public:
  ~MultistreamSource() override {}

  // Returns the number of streams the source produces.
  virtual size_t stream_count() const = 0;

  // Gets a packet for the stream indicated via stream_index_out. This call
  // should always produce a packet until end-of-stream. The caller is
  // responsible for releasing the packet.
  virtual PacketPtr PullPacket(size_t* stream_index_out) = 0;
};

}  // namespace media
}  // namespace mojo

#endif  // SERVICES_MEDIA_FRAMEWORK_MODELS_MULTISTREAM_SOURCE_H_
