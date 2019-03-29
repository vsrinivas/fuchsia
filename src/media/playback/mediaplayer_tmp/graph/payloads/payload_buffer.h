// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_PAYLOADS_PAYLOAD_BUFFER_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_PAYLOADS_PAYLOAD_BUFFER_H_

#include <fbl/recycler.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <lib/fit/function.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>
#include <memory>
#include "src/lib/fxl/logging.h"
#include "src/media/playback/mediaplayer_tmp/graph/payloads/fifo_allocator.h"

namespace media_player {

class VmoPayloadAllocator;

// A VMO used for payload buffers.
class PayloadVmo : public fbl::RefCounted<PayloadVmo> {
 public:
  // Creates a VMO and wraps it with a |PayloadVmo|. If |bti_handle| is
  // provided, the VMO is created with |zx_vmo_create_contiguous|.
  // TODO(dalesat): Remove |bti_handle| when the fidl buffer allocator happens.
  static fbl::RefPtr<PayloadVmo> Create(uint64_t vmo_size,
                                        const zx::handle* bti_handle = nullptr);

  // Creates a |PayloadVmo| that wraps the provided VMO.
  static fbl::RefPtr<PayloadVmo> Create(zx::vmo vmo, zx_vm_option_t map_flags);

  PayloadVmo(zx::vmo vmo, uint64_t vmo_size, zx_vm_option_t map_flags,
             const zx::handle* bti_handle, zx_status_t* status_out);

  ~PayloadVmo() = default;

  // Returns the size of the VMO in bytes.
  uint64_t size() const { return size_; }

  // Returns the address in process virtual memory where this VMO is mapped, if
  // it is mapped, nullptr otherwise.
  void* start() const { return vmo_mapper_.start(); }

  // The index of this VMO in the allocator's vector of VMOs.
  uint32_t index() { return index_; }

  // Sets the index of this VMO in the allocator's vector of VMOs.
  void SetIndex(uint32_t index) { index_ = index; }

  // Returns |start()| offset by |offset| if the VMO is mapped, nullptr
  // otherwise.
  void* at_offset(uint64_t offset) {
    FXL_DCHECK(offset < size_);

    if (start() == nullptr) {
      return nullptr;
    }

    return reinterpret_cast<uint8_t*>(start()) + offset;
  }

  // Returns a reference to the VMO.
  zx::vmo& vmo() { return vmo_; }
  const zx::vmo& vmo() const { return vmo_; }

  // Duplicates the VMO creating a new VMO handle with the specified rights.
  zx::vmo Duplicate(zx_rights_t rights);

 private:
  zx::vmo vmo_;
  uint64_t size_;
  uint32_t index_;

  fzl::VmoMapper vmo_mapper_;

  // NOTE: Access to these two fields is serialized using the mutex on the
  // owning |VmoPayloadAllocator|.
  bool allocated_ = false;
  fbl::unique_ptr<FifoAllocator> allocator_;

  friend class VmoPayloadAllocator;
};

// A buffer used to hold a packet payload.
//
// A |PayloadBuffer| instance is managed using |fbl::RefPtr| and has an
// associated recycler, which is responsible for freeing the memory that the
// |PayloadBuffer| encapsulates. When the last reference to a |PayloadBuffer|
// is dropped, the recycler is called.
class PayloadBuffer final : public fbl::RefCounted<PayloadBuffer>,
                            public fbl::Recyclable<PayloadBuffer> {
 public:
  // All payload buffers must be aligned on |kByteAlignment|-byte boundaries.
  static constexpr size_t kByteAlignment = 32;

  // Returns the smallest multiple of |kByteAlignment| that is no smaller than
  // |size|.
  static size_t AlignUp(size_t size) {
    return size = (size + kByteAlignment - 1) & ~(kByteAlignment - 1);
  }

  // Indicates whether |buffer| is aligned to |kByteAlignment| bytes.
  static bool IsAligned(void* buffer) {
    return (reinterpret_cast<uintptr_t>(buffer) & (kByteAlignment - 1)) == 0;
  }

  // Function type used to recycle a |PayloadBuffer|. The |PayloadBuffer|
  // deletes itself, so the recycler should not attempt to delete it.
  using Recycler = fit::function<void(PayloadBuffer*)>;

  // Function type used for |AfterRecycling|.
  using Action = fit::function<void(PayloadBuffer*)>;

  // Creates a new |PayloadBuffer|. |size| may not be 0, and |data| may not be
  // nullptr.
  static fbl::RefPtr<PayloadBuffer> Create(uint64_t size, void* data,
                                           Recycler recycler);

  // Creates a new |PayloadBuffer|. |size| may not be 0, and |data| may not be
  // nullptr. |offset_in_vmo| gives the offset of the buffer with respect to
  // the start of the VMO. This should be (data - vmo.start()). This
  // redundancy is for future support of VMOs that can't be mapped.
  // TODO(dalesat): Support null data for payloads that can't be mapped.
  static fbl::RefPtr<PayloadBuffer> Create(uint64_t size, void* data,
                                           fbl::RefPtr<PayloadVmo> vmo,
                                           uint64_t offset_in_vmo,
                                           Recycler recycler);

  static fbl::RefPtr<PayloadBuffer> CreateWithMalloc(uint64_t size);

  ~PayloadBuffer();

  // Returns the size in bytes of the buffer, which will never be 0.
  uint64_t size() const { return size_; }

  // Returns a pointer to the buffer, which will never be nullptr.
  void* data() const { return data_; }

  // Returns the |PayloadVmo| containing the buffer, if the buffer was allocated
  // from a VMO, nullptr otherwise.
  PayloadVmo* vmo() const { return vmo_.get(); }

  // Returns the offset of the data in the VMO, if the buffer was allocated from
  // a VMO, zero otherwise.
  uint64_t offset() const { return offset_; }

  // Returns the ID of this |PayloadBuffer|. This value is set by the party
  // that creates the buffer, and its semantics are defined by that party. It
  // is intended to identify the buffer and may correspond to the fidl field
  // media.StreamPacket.payload_buffer_id or
  // mediacodec.CodecBuffer.buffer_index.
  uint32_t id() const { return id_; }

  // Sets the ID of this |PayloadBuffer|.
  void SetId(uint32_t id) { id_ = id; }

  // Returns the buffer config of this |PayloadBuffer|. This value is set by the
  // party that creates the buffer, and its semantics are defined by that party.
  // It is intended to identify the buffer configuration and may correspond to
  // the fidl field media.StreamPacket.buffer_config or
  // mediacodec.CodecBuffer.buffer_lifetime_ordinal.
  uint64_t buffer_config() const { return buffer_config_; }

  // Sets the ID of this |PayloadBuffer|.
  void SetBufferConfig(uint64_t buffer_config) {
    buffer_config_ = buffer_config;
  }

  // Registers a function to be called after recycling. This method may only
  // be called once on a given instance. An |Action| should not hold a reference
  // to the |PayloadBuffer|, because this would produce a circular reference,
  // and the |PayloadBuffer| would never be released.
  void AfterRecycling(Action action);

 private:
  PayloadBuffer(uint64_t size, void* data, Recycler recycler);

  PayloadBuffer(uint64_t size, void* data, fbl::RefPtr<PayloadVmo> vmo,
                uint64_t offset, Recycler recycler);

  void fbl_recycle();

  uint64_t size_;
  void* data_;
  fbl::RefPtr<PayloadVmo> vmo_;
  uint64_t offset_;
  uint32_t id_ = 0;
  uint64_t buffer_config_ = 0;
  Recycler recycler_;
  Action after_recycling_;

  friend class fbl::Recyclable<PayloadBuffer>;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_PAYLOADS_PAYLOAD_BUFFER_H_
