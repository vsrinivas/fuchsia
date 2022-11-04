// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FZL_VMO_POOL_H_
#define LIB_FZL_VMO_POOL_H_

#include <lib/fzl/pinned-vmo.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include <limits>
#include <optional>

#include <fbl/array.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/vector.h>

#include "lib/stdcompat/span.h"

namespace fzl {

// This class is not thread safe.
// VmoPool is intended to be used by content producers who have the following
// usage pattern regarding a collection of VMOs:
// Setup: A producer and at least one consumer establish a connection and share a
//    homogenous set of VMOs. A common way to do this is through BufferCollections
//    and the Sysmem library.
// During normal operation:
// 1) The producer obtains a write lock on a free vmo.
// 2) The producer writes into the VMO.  Multiple write-locked VMOs may be held
//    simultaneously.
// 3) When the producer is finished writing to the VMO, it signals the consumer
//    that the VMO is ready to be consumed. The VMO is now read-locked.
// 4) When the VMO is finished being consumed, the consumer signals the producer
//    that it is done with the vmo.  The producer then marks that VMO as free.
//
// VmoPool maintains the bookkeeping for the above interaction, as follows:
// 1) The producer calls LockBufferForWrite(), which returns a Buffer object.
// 2) The valid Buffer object represents a write lock.
// 3) When the producer is done writing, it calls ReleaseWriteLockAndGetIndex
//    which returns the index of the buffer.  This index can be sent to the
//    consumer to signal that a buffer is ready to be consumed.  Calling
//    ReleaseWriteLockAndGetIndex invalidates the Buffer object and constitutes
//    a read lock on the VMO.
// 4) When the VMO is finished being consumed, the consumer send the index back
//    to the producer, who then calls ReleaseBuffer on that index, marking the
//    VMO as free.
//
// VmoPool additionally handles the mapping and pinning of VMOs through the
// MapVmos and PinVmos functions.  After the buffers have been mapped/pinned,
// the virtual/physical address can be accessed through the Buffer instances.
class VmoPool {
 public:
  // Options for pinning VMOs:
  // Require contiguous memory:
  enum class RequireContig : bool { No = false, Yes };
  // Require that the physical memory address be expressable as a 32bit
  // unsigned integer:
  enum class RequireLowMem : bool { No = false, Yes };

  // Initializes the VmoPool with a set of vmos.
  zx_status_t Init(cpp20::span<zx::unowned_vmo> vmos);

  // Pin all the vmos to physical memory.  This must be called prior to
  // requesting a physical address from any Buffer instance.
  zx_status_t PinVmos(const zx::bti& bti, RequireContig req_contiguous,
                      RequireLowMem req_low_memory);

  // Map the vmos to virtual memory. This must be called prior to
  // requesting a virtual address from any Buffer instance.
  zx_status_t MapVmos();

  // Resets the buffer read and write locks.
  void Reset();

  // Finds the next available buffer, locks that buffer for writing, and
  // returns a Buffer instance to allow access to that buffer.
  // If no buffers are available, returns an empty std::optional.
  class Buffer;
  std::optional<Buffer> LockBufferForWrite();

  // Unlocks the buffer with the specified index and sets it as ready to be
  // reused. Calling ReleaseBuffer with the index from
  // buffer.ReleaseWriteLockAndGetIndex() is equivalent to calling
  // buffer.Release().
  // Returns ZX_OK if successful, or ZX_ERR_NOT_FOUND if no locked buffer
  // was found with the given index. If the index is out of bounds,
  // ZX_ERROR_INVALID_ARGS will be returned.
  zx_status_t ReleaseBuffer(uint32_t buffer_index);

  // Returns the total number of buffers in this pool.
  size_t total_buffers() const { return buffers_.size(); }

  // Returns the number of free buffers in this pool.
  size_t free_buffers() const { return free_buffers_.size(); }

  // Returns the size (in bytes) of the buffer at a given index in this pool.
  size_t buffer_size(uint32_t buffer_index = 0) const {
    ZX_DEBUG_ASSERT(buffer_index < buffers_.size());
    return buffers_[buffer_index].buffer_size;
  }

  ~VmoPool();

  // The Buffer class offers an object-oriented way to accessing the buffers
  // within VmoPool.  Retaining ownership of a valid Buffer corresponds to
  // a write lock. To release the write lock, you can:
  //  - Call ReleaseWriteLockAndGetIndex().  This releases the write
  //    lock, and returns the index of the buffer, for use with fidl interfaces with
  //    shared arrays of VMOs, such as ImagePipe, or camera::Stream::FrameAvailable.
  //    The buffer is now read locked.
  //  - Call Release().  This releases the buffer completely.
  //  - Allow the Buffer object to go out of scope.  This is equivalent to calling
  //    Release().
  class Buffer {
   public:
    // Default constructor creates an invalid Buffer:
    Buffer() : Buffer(nullptr, 0) {}

    ~Buffer();

    // Only allow move constructors.
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    // Release the buffer from its write lock, which puts the Buffer instance
    // in the invalid state. If the Buffer was in a valid state before the call,
    // this will return the index of the Buffer. Calling ReleaseBuffer with
    // this index is now the only way to release the buffer.
    // Asserts that the Buffer is valid.
    uint32_t ReleaseWriteLockAndGetIndex();

    // Releases buffer back to free pool.
    zx_status_t Release();

    // Returns the size of the buffer. Asserts that the Buffer instance is
    // valid.
    size_t size() const;

    // Return the virtual address to the start of the buffer.
    // Asserts that the buffer is mapped, and that the Buffer instance
    // is valid.
    void* virtual_address() const;

    // Return the physical address of the start of the buffer.a
    //
    // Asserts that the buffer is pinned, and that the Buffer instance is
    // valid.
    zx_paddr_t physical_address() const;

    // Return the vmo handle.
    // Asserts that the Buffer instance is valid.
    zx_handle_t vmo_handle() const;

    bool valid() const { return pool_ != nullptr; }

   private:
    Buffer(VmoPool* pool, uint32_t index) : pool_(pool), index_(index) {}

    // Remove the copy and assignment constructor.
    Buffer(const Buffer&) = delete;
    void operator=(const Buffer&) = delete;

    // The only function which should construct a Buffer from an index and
    // a pool is VmoPool::LockBufferForWrite.
    friend std::optional<Buffer> VmoPool::LockBufferForWrite();

    // The validity of the pool pointer indicates whether the buffer
    // instance itself is valid.
    VmoPool* pool_ = nullptr;
    uint32_t index_ = 0;
  };

  // Buffer is allowed to access the internals of VmoPool to access
  // ListableBuffers.
  friend class Buffer;

 private:
  struct ListableBuffer
      : public fbl::SinglyLinkedListable<ListableBuffer*, fbl::NodeOptions::AllowClearUnsafe> {
    // Get the start of the virtual memory address.
    void* virtual_address() const;
    // Get the start of the physical address.
    zx_paddr_t physical_address() const;

    zx_status_t PinVmo(const zx::bti& bti, RequireContig require_contiguous,
                       RequireLowMem require_low_memory);

    zx_status_t MapVmo();

    VmoMapper mapped_buffer;
    PinnedVmo pinned_buffer;
    zx::vmo vmo;
    uint64_t buffer_size = 0;
    bool is_mapped = false;
    bool is_pinned = false;
  };

  // VMO / mapping / pinning backing each buffer.
  fbl::Array<ListableBuffer> buffers_;
  // The list of free buffers.
  fbl::SizedSinglyLinkedList<ListableBuffer*> free_buffers_;
};

}  // namespace fzl

#endif  // LIB_FZL_VMO_POOL_H_
