// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FIDL_BUFFER_SET_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FIDL_BUFFER_SET_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fit/thread_checker.h>
#include <lib/syslog/cpp/macros.h>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/playback/mediaplayer/graph/payloads/payload_allocator.h"
#include "src/media/playback/mediaplayer/graph/payloads/payload_buffer.h"

namespace media_player {

// A set of buffers associated with a specific StreamBufferSettings and
// buffer lifetime ordinal.
//
// This class is thread-safe.
class BufferSet : public fbl::RefCounted<BufferSet> {
 public:
  // Creates a buffer set with the specified settings and lifetime ordinal.
  // |single_vmo| indicates whether the buffers should be allocated from a
  // single VMO (true) or a VMO per buffer.
  static fbl::RefPtr<BufferSet> Create(const fuchsia::media::StreamBufferSettings& settings,
                                       uint64_t lifetime_ordinal, bool single_vmo);

  BufferSet(const fuchsia::media::StreamBufferSettings& settings, uint64_t lifetime_ordinal,
            bool single_vmo);

  ~BufferSet();

  // Sets the buffer count.
  void SetBufferCount(uint32_t buffer_count);

  // Gets the partial settings for this buffer set. The |buffer_lifetime_ordinal| of settings is set
  // to the |lifetime_ordinal| value passed into the constructor.
  fuchsia::media::StreamBufferPartialSettings PartialSettings(
      fuchsia::sysmem::BufferCollectionTokenPtr token) const;

  // Sets the value passed into the constructor as |single_vmo|.
  bool single_vmo() const {
    std::lock_guard<std::mutex> locker(mutex_);
    return single_vmo_;
  }

  // Returns the buffer lifetime ordinal passed to the constructor.
  uint64_t lifetime_ordinal() const {
    std::lock_guard<std::mutex> locker(mutex_);
    return lifetime_ordinal_;
  }

  uint32_t packet_count_for_server() {
    std::lock_guard<std::mutex> locker(mutex_);
    return packet_count_for_server_;
  }

  uint32_t packet_count_for_client() {
    std::lock_guard<std::mutex> locker(mutex_);
    return packet_count_for_client_;
  }

  // Returns the size in bytes of the buffers in this set.
  uint32_t buffer_size() const {
    std::lock_guard<std::mutex> locker(mutex_);
    return buffer_size_;
  }

  // Returns the number of buffers in the set.
  uint32_t buffer_count() const {
    std::lock_guard<std::mutex> locker(mutex_);
    return buffers_.size();
  }

  // Allocates a buffer.
  fbl::RefPtr<PayloadBuffer> AllocateBuffer(uint64_t size, const PayloadVmos& payload_vmos);

  // Adds a reference to the payload buffer on behalf of the outboard processor.
  // |payload_buffer| cannot be null. This version is used when the client
  // has a reference to the |PayloadBuffer| already.
  void AddRefBufferForProcessor(uint32_t buffer_index, fbl::RefPtr<PayloadBuffer> payload_buffer);

  // Takes a reference to the payload buffer previously added using
  // |AddRefBufferForProcessor| or |AllocateAllBuffersForProcessor| and returns a
  // reference to the |PayloadBuffer|.
  fbl::RefPtr<PayloadBuffer> TakeBufferFromProcessor(uint32_t buffer_index);

  // Gets a new reference to a buffer already owned by the outboard processor.
  fbl::RefPtr<PayloadBuffer> GetProcessorOwnedBuffer(uint32_t buffer_index);

  // Allocates all buffers for the outboard processor. All buffers must be free
  // when this method is called.
  void AllocateAllBuffersForProcessor(const PayloadVmos& payload_vmos);

  // Releases all buffers currently owned by the output processor.
  void ReleaseAllProcessorOwnedBuffers();

  // Returns true if there's a free buffer, otherwise calls |callback| on an
  // arbirary thread when one becomes free. The pending action can be cancelled
  // by calling |CancelPendingFreeBufferCallback|.
  bool HasFreeBuffer(fit::closure callback);

  // Indicates that this |BufferSet| has been parked in favor of a new one.
  // After decommissioning and When all its buffers have been recycled, the
  // buffer set will be deleted.
  void Decommission();

 private:
  // The current state of a buffer in the set.
  struct BufferInfo {
    // Indicates whether the buffer is free. |processor_ref_| must be false if
    // this field is true.
    bool free_ = true;

    // This field is non-null for buffers that are currently owned by the
    // outboard processor.
    fbl::RefPtr<PayloadBuffer> processor_ref_;
  };

  // Creates a |PayloadBuffer| for the indicated |buffer_index|.
  fbl::RefPtr<PayloadBuffer> CreateBuffer(uint32_t buffer_index,
                                          const std::vector<fbl::RefPtr<PayloadVmo>>& payload_vmos)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  mutable std::mutex mutex_;

  uint64_t lifetime_ordinal_ FXL_GUARDED_BY(mutex_);
  bool single_vmo_ FXL_GUARDED_BY(mutex_);
  uint64_t buffer_constraints_version_ordinal_ FXL_GUARDED_BY(mutex_);
  bool single_buffer_mode_ FXL_GUARDED_BY(mutex_);
  uint32_t packet_count_for_server_ FXL_GUARDED_BY(mutex_);
  uint32_t packet_count_for_client_ FXL_GUARDED_BY(mutex_);
  uint32_t buffer_size_ FXL_GUARDED_BY(mutex_);

  std::vector<BufferInfo> buffers_ FXL_GUARDED_BY(mutex_);

  // |suggest_next_to_allocate_| suggests the next buffer to allocate. When
  // allocating a buffer, a sequential search for a free buffer starts at this
  // index, and this index is left referring to the buffer after the allocated
  // buffer (with wraparound). Given the normally FIFO behavior of the caller,
  // only one increment is typically required per allocation. This approach
  // tends to allocate the buffers in a round-robin fashion.
  uint32_t suggest_next_to_allocate_ FXL_GUARDED_BY(mutex_) = 0;
  uint32_t free_buffer_count_ FXL_GUARDED_BY(mutex_) = 0;

  fit::closure free_buffer_callback_ FXL_GUARDED_BY(mutex_);

  // Disallow copy and assign.
  BufferSet(const BufferSet&) = delete;
  BufferSet& operator=(const BufferSet&) = delete;
};

// Manages a sequence of buffer sets.
//
// This class is not thread-safe. The constructor, desctructor and all methods
// must be called on the same thread.
class BufferSetManager {
 public:
  BufferSetManager() = default;

  ~BufferSetManager() { FIT_DCHECK_IS_THREAD_VALID(thread_checker_); };

  // Determines whether this has a current buffer set.
  bool has_current_set() const {
    FIT_DCHECK_IS_THREAD_VALID(thread_checker_);
    return !!current_set_;
  }

  // The current buffer set. Do not call this method when |has_current| returns
  // false.
  BufferSet& current_set() {
    FIT_DCHECK_IS_THREAD_VALID(thread_checker_);
    FX_DCHECK(current_set_);
    return *current_set_;
  }

  // Applies the specified constraints, creating a new buffer set. If
  // |single_vmo_preferred| and |single_buffer_mode_allowed| are true, one vmo
  // will be used for all the new buffers. Otherwise, each new buffer will have
  // its own vmo. The resulting set's |single_vmo| method with return true in
  // former case, false in the latter.
  //
  // Returns whether the constraints were successfully applied.
  bool ApplyConstraints(const fuchsia::media::StreamBufferConstraints& constraints,
                        bool single_vmo_preferred);

  // Releases a reference to the payload buffer previously added using
  // |BufferSet::AddRefBufferForProcessor| or
  // |BufferSet::AllocateAllBuffersForProcessor|.
  void ReleaseBufferForProcessor(uint64_t lifetime_ordinal, uint32_t buffer_index);

 private:
  FIT_DECLARE_THREAD_CHECKER(thread_checker_);

  // The current |BufferSet| this is only null when |ApplyConstraints| has
  // never been called. It's important not to clear this arbitrarily, because
  // that would prevent the buffer lifetime ordinals from progressing correctly.
  fbl::RefPtr<BufferSet> current_set_;

  // Disallow copy and assign.
  BufferSetManager(const BufferSetManager&) = delete;
  BufferSetManager& operator=(const BufferSetManager&) = delete;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FIDL_BUFFER_SET_H_
