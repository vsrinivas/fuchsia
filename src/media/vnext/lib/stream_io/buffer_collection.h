// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_STREAM_IO_BUFFER_COLLECTION_H_
#define SRC_MEDIA_VNEXT_LIB_STREAM_IO_BUFFER_COLLECTION_H_

#include <fuchsia/media2/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>
#include <lib/fpromise/scope.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/sync/completion.h>
#include <lib/zx/vmo.h>

#include <vector>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/vnext/lib/stream_io/payload_buffer.h"

namespace fmlib {

// Base class for |OutputBufferCollection| and |InputBufferCollection|, which are used when payload
// buffers must be mapped into process memory.
class BufferCollection {
 public:
  virtual ~BufferCollection() = default;

  // Returns duplicates of the VMOs managed by this |BufferCollection|. This method is thread-safe.
  std::vector<zx::vmo> DuplicateVmos(zx_rights_t rights) const;

 protected:
  // A payload buffer VMO held by this |BufferCollection|.
  struct BufferVmo {
    BufferVmo(zx::vmo vmo, zx_vm_option_t map_flags);

    // The status returned by the initialization of this |BufferVmo|.
    zx_status_t status() const { return status_; }

    const zx::vmo& vmo() const { return vmo_; }
    zx::vmo& vmo() { return vmo_; }

    bool is_valid() const { return status_ == ZX_OK; }

    // Returns the address in process virtual memory where this VMO is mapped.
    void* data() const { return vmo_mapper_.start(); }

    // Returns the size of this VMO in bytes.
    size_t size() const { return vmo_mapper_.size(); }

    // Returns |data()| offset by |offset| bytes.
    void* at_offset(size_t offset) const;

    // Determines if this |BufferVmo| is currently allocated.
    bool is_allocated() const { return allocated_; }

    // Indicates this |BufferVmo| is now allocated.
    void Allocate() { allocated_ = true; }

    // Indicates this |BufferVmo| is now free.
    void Free() { allocated_ = false; }

   private:
    zx_status_t status_;
    zx::vmo vmo_;
    fzl::VmoMapper vmo_mapper_;
    bool allocated_ = false;
  };

  // Constructs a |BufferCollection|.
  explicit BufferCollection(std::vector<BufferVmo> buffer_vmos)
      : buffer_vmos_(std::move(buffer_vmos)) {}

  // Calls |GetBuffers| on the buffer provider and returns a promise that completes when the
  // provider responds.
  [[nodiscard]] static fpromise::promise<std::vector<zx::vmo>, fuchsia::media2::BufferProviderError>
  GetBuffers(fuchsia::media2::BufferProvider& provider, zx::eventpair token,
             const fuchsia::media2::BufferConstraints& constraints, const std::string& name,
             uint64_t id, zx_vm_option_t map_flags);

  // Creates a vector of |BufferVmo|s from a vector of |zx::vmo|s. |vmos| must not be empty.
  // Returns an empty array if mapping fails for any of |vmos|.
  static std::vector<BufferVmo> CreateBufferVmos(std::vector<zx::vmo> vmos,
                                                 zx_vm_option_t map_flags);

  std::vector<BufferVmo>& buffer_vmos() FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    return buffer_vmos_;
  }
  const std::vector<BufferVmo>& buffer_vmos() const FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    return buffer_vmos_;
  }

  mutable std::mutex mutex_;

 private:
  std::vector<BufferVmo> buffer_vmos_ FXL_GUARDED_BY(mutex_);
  fpromise::scope scope_;
};

// A |BufferCollection| to be used for outputs. This subclass provides methods for allocating
// payload buffers.
class OutputBufferCollection : public BufferCollection {
 public:
  // Starts creating an |OutputBufferCollection| and returns a promise that returns the collection
  // when the it is ready to use. |provider| must be valid until the operation completes. This
  // method is thread-safe, and the returned promise may be run on any thread. |executor| is used to
  // run promises when payload buffers are released.
  [[nodiscard]] static fpromise::promise<std::unique_ptr<OutputBufferCollection>,
                                         fuchsia::media2::ConnectionError>
  Create(async::Executor& executor, fuchsia::media2::BufferProvider& provider, zx::eventpair token,
         const fuchsia::media2::BufferConstraints& constraints, const std::string& name,
         uint64_t id, zx_vm_option_t map_flags = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);

  ~OutputBufferCollection() override;

  // Allocates a |PayloadBuffer| of the specified size. Returns an invalid |PayloadBuffer| if the
  // buffer collection is currently exhausted. |size| must be greater than zero and less than or
  // equal to the buffer size. This method is thread-safe.
  // TODO(dalesat): Consider using std::optional here rather than having invalid PayloadBuffers.
  PayloadBuffer AllocatePayloadBuffer(size_t size);

  // Allocates a |PayloadBuffer| of the specified size when one becomes available, blocking in the
  // the mean time. Returns an invalid |PayloadBuffer| if |FailPendingAllocation| is called. |size|
  // must be greater than zero and less than or equal to the buffer size. This method is
  // thread-safe, but may not be called on thread represented by the executor passed to the
  // constructor.
  // TODO(dalesat): Consider separating Blocking from WhenAvailable so only one can be used.
  PayloadBuffer AllocatePayloadBufferBlocking(size_t size);

  // Returns a promise that completes with an allocated |PayloadBuffer| of the specified size when
  // one becomes available. Completes with an invalid |PayloadBuffer| if |FailPendingAllocation| is
  // called. |size| must be greater than zero and less than or equal to the buffer size. This method
  // is thread-safe. This method must not be called when a promise from a previous call is still
  // pending.
  [[nodiscard]] fpromise::promise<PayloadBuffer> AllocatePayloadBufferWhenAvailable(size_t size);

  // Causes any allocation (|AllocatePayloadBufferBlocking| or |AllocatePayloadBufferWhenAvailable|)
  // that is currently pending buffer availability to fail. This method is thread-safe.
  void FailPendingAllocation();

  // Gets a closure that calls |FailPendingAllocation| on this collection as long as this collection
  // exists, does nothing thereafter.
  fit::closure GetFailPendingAllocationClosure();

 private:
  // Used for calls to |FailPendingAllocation| by non-owners.
  class ClosureContext {
   public:
    explicit ClosureContext(OutputBufferCollection* collection) : collection_(collection) {
      FX_CHECK(collection);
    }

    ~ClosureContext() = default;

    // Fails a pending allocation if |collection_| is still set and the collection has a pending
    // allocation.
    void FailPendingAllocation() {
      std::lock_guard<std::mutex> locker(mutex_);
      if (collection_) {
        collection_->FailPendingAllocation();
      }
    }

    // Clears |collection_|.
    void Reset() {
      std::lock_guard<std::mutex> locker(mutex_);
      collection_ = nullptr;
    }

   private:
    mutable std::mutex mutex_;
    OutputBufferCollection* collection_ FXL_GUARDED_BY(mutex_);
  };

  OutputBufferCollection(async::Executor& executor, std::vector<BufferVmo> buffer_vmos)
      : BufferCollection(std::move(buffer_vmos)), executor_(executor) {}

  PayloadBuffer AllocatePayloadBufferUnsafe(size_t size) FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Determines whether a buffer of the specified size is currently available.
  bool BufferAvailableUnsafe(size_t size) FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  async::Executor& executor_;
  size_t free_vmo_guess_ FXL_GUARDED_BY(mutex_) = 0;
  fpromise::completer<> when_available_completer_ FXL_GUARDED_BY(mutex_);
  size_t when_available_size_ FXL_GUARDED_BY(mutex_);
  sync_completion completion_;
  std::shared_ptr<ClosureContext> closure_context_ FXL_GUARDED_BY(mutex_);
  fpromise::scope scope_;
};

// A |BufferCollection| to be used for inputs. This subclass provides a method for obtaining a
// mapped payload buffer.
class InputBufferCollection : public BufferCollection {
 public:
  // Starts creating an |InputBufferCollection| and returns a promise that returns the collection
  // when the it is ready to use. |provider| must be valid until the operation completes. This
  // method is thread-safe, and the returned promise may be run on any thread.
  [[nodiscard]] static fpromise::promise<std::unique_ptr<InputBufferCollection>,
                                         fuchsia::media2::ConnectionError>
  Create(fuchsia::media2::BufferProvider& provider, zx::eventpair token,
         const fuchsia::media2::BufferConstraints& constraints, const std::string& name,
         uint64_t id, zx_vm_option_t map_flags = ZX_VM_PERM_READ);

  ~InputBufferCollection() override = default;

  // Gets the |PayloadBuffer| described by |payload_range|. If |payload_range| isn't valid for this
  // collection, returns an invalid |PayloadBuffer|. This method is thread-safe.
  PayloadBuffer GetPayloadBuffer(const fuchsia::media2::PayloadRange& payload_range);

 private:
  explicit InputBufferCollection(std::vector<BufferVmo> buffer_vmos)
      : BufferCollection(std::move(buffer_vmos)) {}
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_STREAM_IO_BUFFER_COLLECTION_H_
