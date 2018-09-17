// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_FIDL_BUFFER_SET_H_
#define GARNET_BIN_MEDIAPLAYER_FIDL_BUFFER_SET_H_

#include <memory>
#include <unordered_map>

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>

#include "garnet/bin/mediaplayer/decode/decoder.h"

namespace media_player {

// A mapped VMO used for one or more buffers in a BufferSet.
// This code is adapted from zircon's VmoMapper.
class BufferVmo {
 public:
  BufferVmo() = default;
  ~BufferVmo() { Reset(); }

  // Create a new VMO and map it into our address space using the provided map
  // flags and optional target VMAR.  If requested, return the created VMO
  // with the requested rights.
  //
  // size         : The minimum size, in bytes, of the VMO to create.
  // map_flags    : The flags to use when mapping the VMO.
  // vmo_rights   : The rights which should be applied to the VMO which is
  //                returned by vmo(), or ZX_RIGHT_SAME_RIGHTS to leave the
  //                default rights.
  // bti_handle   : Handle to use for zx_vmo_create_contiguous if the vmo must
  //                be contiguous. Passing an invalid handle indicates that a
  //                contiguous VMO is not required.
  zx_status_t CreateAndMap(uint64_t size, zx_vm_option_t map_flags,
                           zx_rights_t vmo_rights,
                           const zx::handle& bti_handle);

  // Unmap and reset the VMO.
  void Reset();

  void* start() const { return start_; }
  uint64_t size() const { return size_; }
  zx::vmo& vmo() { return vmo_; }
  const zx::vmo& vmo() const { return vmo_; }

 private:
  zx::vmo vmo_;
  void* start_ = nullptr;
  uint64_t size_ = 0;
};

// A set of buffers associated with a specific CodecPortBufferSettings and
// buffer lifetime ordinal.
class BufferSet {
 public:
  // Creates a buffer set with the specified settings and lifetime ordinal.
  // |single_vmo| indicates whether the buffers should be allocated from a
  // single VMO (true) or a VMO per buffer. If |bti_handle| is supplied, the
  // VMO(s) will be physically contiguous.
  static std::unique_ptr<BufferSet> Create(
      const fuchsia::mediacodec::CodecPortBufferSettings& settings,
      uint64_t lifetime_ordinal, bool single_vmo, const zx::handle& bti_handle);

  BufferSet(const fuchsia::mediacodec::CodecPortBufferSettings& settings,
            uint64_t lifetime_ordinal, bool single_vmo,
            const zx::handle& bti_handle);

  ~BufferSet() = default;

  // Gets the settings for this buffer set. The |buffer_lifetime_ordinal| of
  // settings is set to the |lifetime_ordinal| value passed into the
  // constructor.
  const fuchsia::mediacodec::CodecPortBufferSettings& settings() const {
    return settings_;
  }

  // Returns the buffer lifetime ordinal passed to the constructor.
  uint64_t lifetime_ordinal() const {
    return settings_.buffer_lifetime_ordinal;
  }

  // Returns the size in bytes of the buffers in this set.
  uint32_t buffer_size() const { return settings_.per_packet_buffer_bytes; }

  // Returns the number of buffers in the set.
  uint32_t buffer_count() const { return owners_by_index_.size(); }

  // Returns the number of free buffers.
  uint32_t free_buffer_count() const { return free_buffer_count_; }

  // Returns a |CodecBuffer| struct for the specified buffer. |writeable|
  // determines whether the vmo handle in the descriptor should have write
  // permission.
  fuchsia::mediacodec::CodecBuffer GetBufferDescriptor(uint32_t buffer_index,
                                                       bool writeable) const;

  // Gets a pointer to the data for the specified buffer.
  void* GetBufferData(uint32_t buffer_index) const;

  // Allocates a buffer for the specified party, returning its index.
  uint32_t AllocateBuffer(uint8_t party);

  // Transfers ownership of an allocated buffer to a new party.
  void TransferBuffer(uint32_t buffer_index, uint8_t party);

  // Frees a buffer.
  void FreeBuffer(uint32_t buffer_index);

  // Allocates all free buffers to the specified party.
  void AllocateAllFreeBuffers(uint8_t party);

  // Frees all free buffers currently allocated to the specified party.
  void FreeAllBuffersOwnedBy(uint8_t party);

 private:
  // Gets the BufferVmo for the specified index.
  BufferVmo& buffer_vmo(size_t buffer_index) {
    FXL_DCHECK(buffer_index < buffer_count());
    return buffer_vmos_by_index_ ? buffer_vmos_by_index_[buffer_index]
                                 : single_buffer_vmo_;
  }

  // Gets the BufferVmo for the specified index (const version).
  const BufferVmo& buffer_vmo(size_t buffer_index) const {
    FXL_DCHECK(buffer_index < buffer_count());
    return buffer_vmos_by_index_ ? buffer_vmos_by_index_[buffer_index]
                                 : single_buffer_vmo_;
  }

  fuchsia::mediacodec::CodecPortBufferSettings settings_;
  BufferVmo single_buffer_vmo_;
  std::unique_ptr<BufferVmo[]> buffer_vmos_by_index_;

  // |owners_by_index_| indicates who owns each buffer. 0 indicates the buffer
  // is free. Non-zero values refer to owners assigned by the caller.
  std::vector<uint8_t> owners_by_index_;

  // |suggest_next_to_allocate_| suggests the next buffer to allocate. When
  // allocating a buffer, a sequential search for a free buffer starts at this
  // index, and this index is left referring to the buffer after the allocated
  // buffer (with wraparound). Given the normally FIFO behavior of the caller,
  // only one increment is typically required per allocation. This approach
  // tends to allocate the buffers in a round-robin fashion.
  uint32_t suggest_next_to_allocate_ = 0;
  uint32_t free_buffer_count_ = 0;

  // Disallow copy and assign.
  BufferSet(const BufferSet&) = delete;
  BufferSet& operator=(const BufferSet&) = delete;
};

// Manages a sequence of buffer sets.
class BufferSetManager {
 public:
  BufferSetManager() = default;

  ~BufferSetManager() = default;

  // Determines whether this has a current buffer set.
  bool has_current_set() const { return !!current_set_; }

  // The current buffer set. Do not call this method when |has_current| returns
  // false.
  BufferSet& current_set() {
    FXL_DCHECK(current_set_);
    return *current_set_;
  }

  // Applies the specified constraints, creating a new buffer set. If
  // |single_vmo| is true, one vmo will be used for all the new buffers.
  // Otherwise, each new buffer will have its own vmo.
  void ApplyConstraints(
      const fuchsia::mediacodec::CodecBufferConstraints& constraints,
      bool single_vmo);

  // Frees a buffer with the given lifetime ordinal and index. Returns
  // true if the buffer was from the current set, and the set was previously
  // exhausted.
  bool FreeBuffer(uint64_t lifetime_ordinal, uint32_t buffer_index);

 private:
  std::unique_ptr<BufferSet> current_set_;
  std::unordered_map<uint64_t, std::unique_ptr<BufferSet>> old_sets_by_ordinal_;

  // Disallow copy and assign.
  BufferSetManager(const BufferSetManager&) = delete;
  BufferSetManager& operator=(const BufferSetManager&) = delete;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_FIDL_BUFFER_SET_H_
