// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_PAYLOADS_PAYLOAD_ALLOCATOR_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_PAYLOADS_PAYLOAD_ALLOCATOR_H_

#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>

#include <memory>
#include <vector>

#include "src/media/playback/mediaplayer/graph/payloads/payload_buffer.h"

namespace media_player {

// An allocator for payload buffers.
class PayloadAllocator {
 public:
  PayloadAllocator() = default;

  virtual ~PayloadAllocator() = default;

  // Allocates and returns a |PayloadBuffer| of at least the specified size.
  // Returns nullptr if the allocation fails. Buffers returned by this method
  // will be aligned to |kByteAlignment| bytes.
  virtual fbl::RefPtr<PayloadBuffer> AllocatePayloadBuffer(uint64_t size) = 0;
};

// A collection of VMOs backing a PayloadAllocator.
class PayloadVmos {
 public:
  PayloadVmos() = default;

  virtual ~PayloadVmos() = default;

  virtual std::vector<fbl::RefPtr<PayloadVmo>> GetVmos() const = 0;
};

// A collection of VMOs provided by the client backing a PayloadAllocator.
class PayloadVmoProvision : public PayloadVmos {
 public:
  PayloadVmoProvision() = default;

  virtual ~PayloadVmoProvision() = default;

  // Adds a VMO to the collection, return a pointer to a |PayloadVmo| for the
  // added VMO.
  virtual void AddVmo(fbl::RefPtr<PayloadVmo> vmo) = 0;

  // Removes a VMO from the collection.
  virtual void RemoveVmo(fbl::RefPtr<PayloadVmo> payload_vmo) = 0;

  // Removes all VMOs from the collection.
  virtual void RemoveAllVmos() = 0;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_PAYLOADS_PAYLOAD_ALLOCATOR_H_
