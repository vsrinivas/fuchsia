// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits>
#include <map>
#include <memory>
#include <vector>

#include <magenta/types.h>
#include <mx/vmo.h>

#include "apps/media/lib/transport/mapped_shared_buffer.h"

namespace media {

// SharedBufferSet simplifies the use of multiple shared buffers by taking care
// of mapping/unmapping and by providing offset/pointer translation. It can be
// used directly when the caller needs to use shared buffers supplied by another
// party. Its subclass SharedBufferSetAllocator can be used by callers that want
// to allocate from a set of shared buffers.
//
// MediaPacketConsumer implementations such as MediaPacketConsumerBase and its
// subclasses should used SharedBufferSet, while producer implementation such
// as MediaPacketProducerBase should use SharedBufferSetAllocator.
class SharedBufferSet {
 public:
  // References an allocation by buffer id and offset into the buffer.
  class Locator {
   public:
    static Locator Null() { return Locator(); }

    Locator() : buffer_id_(0), offset_(kNullOffset) {}

    Locator(uint32_t buffer_id, uint64_t offset)
        : buffer_id_(buffer_id), offset_(offset) {}

    uint32_t buffer_id() const { return buffer_id_; }
    uint64_t offset() const { return offset_; }

    bool is_null() const { return offset_ == kNullOffset; }

    explicit operator bool() const { return !is_null(); }

    bool operator==(const Locator& other) const {
      return buffer_id_ == other.buffer_id() && offset_ == other.offset();
    }

   private:
    static const uint64_t kNullOffset = std::numeric_limits<uint64_t>::max();

    uint32_t buffer_id_;
    uint64_t offset_;
  };

  // Constructs a SharedBufferSet. |local_map_flags| specifies flags used to
  // map vmos for local access.
  SharedBufferSet(uint32_t local_map_flags);

  virtual ~SharedBufferSet();

  // Adds the indicated buffer.
  mx_status_t AddBuffer(uint32_t buffer_id, mx::vmo vmo);

  // Creates a new buffer of the indicated size. If successful, delivers the
  // buffer id assigned to the buffer and a vmo to the buffer via
  // |buffer_id_out| and |out_vmo|. |vmo_rights| specifies the rights for
  // |out_vmo|.
  mx_status_t CreateNewBuffer(uint64_t size,
                              uint32_t* buffer_id_out,
                              mx_rights_t vmo_rights,
                              mx::vmo* out_vmo);

  // Removes a buffer.
  void RemoveBuffer(uint32_t buffer_id);

  // Resets the object to its initial state.
  void Reset();

  // Validates a locator and size, verifying that the locator's buffer id
  // references an active buffer and that the locator's offset and size
  // describe a region within the bounds of that buffer.
  bool Validate(const Locator& locator, uint64_t size) const;

  // Translates a locator into a pointer.
  void* PtrFromLocator(const Locator& locator) const;

  // Translates a pointer into a locator.
  Locator LocatorFromPtr(void* ptr) const;

 private:
  // Allocates an unused buffer id.
  uint32_t AllocateBufferId();

  // Adds a buffer to |buffers_| and |buffer_ids_by_base_address_|.
  void AddBuffer(uint32_t buffer_id, MappedSharedBuffer* mapped_shared_buffer);

  uint32_t local_map_flags_;
  std::vector<std::unique_ptr<MappedSharedBuffer>> buffers_;
  std::map<uint8_t*, uint32_t> buffer_ids_by_base_address_;
};

}  // namespace media
