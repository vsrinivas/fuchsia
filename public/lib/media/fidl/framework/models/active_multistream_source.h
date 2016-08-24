// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_FRAMEWORK_MODELS_ACTIVE_MULTISTREAM_SOURCE_H_
#define APPS_MEDIA_SERVICES_FRAMEWORK_MODELS_ACTIVE_MULTISTREAM_SOURCE_H_

#include "apps/media/services/framework/models/part.h"
#include "apps/media/services/framework/packet.h"

namespace mojo {
namespace media {

// Asynchronous source of packets for multiple streams.
class ActiveMultistreamSource : public Part {
 public:
  using SupplyCallback =
      std::function<void(size_t output_index, PacketPtr packet)>;

  ~ActiveMultistreamSource() override {}

  // TODO(dalesat): Support dynamic output creation.

  // Returns the number of streams the source produces.
  virtual size_t stream_count() const = 0;

  // Sets the callback that supplies a packet asynchronously.
  virtual void SetSupplyCallback(const SupplyCallback& supply_callback) = 0;

  // Requests a packet from the source to be supplied asynchronously via
  // the supply callback.
  virtual void RequestPacket() = 0;
};

}  // namespace media
}  // namespace mojo

#endif  // APPS_MEDIA_SERVICES_FRAMEWORK_MODELS_ACTIVE_MULTISTREAM_SOURCE_H_
