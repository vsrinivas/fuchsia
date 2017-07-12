// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SERVICES_BUFFERS_CPP_BUFFER_CONSUMER_H_
#define APPS_MOZART_SERVICES_BUFFERS_CPP_BUFFER_CONSUMER_H_

#include <magenta/types.h>
#include <mx/eventpair.h>

#include <memory>
#include <unordered_map>

#include "apps/mozart/services/buffers/buffer.fidl.h"
#include "apps/mozart/services/buffers/cpp/buffer_fence.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/tasks/message_loop_handler.h"
#include "lib/mtl/vmo/shared_vmo.h"

namespace mozart {

class ConsumedBufferHolder;
class ConsumedBufferRegistry;

// Assists with consuming buffers and monitoring their fences.
//
// This object is bound to the current message loop thread and can only
// be used on that thread.
class BufferConsumer : private mtl::MessageLoopHandler {
 public:
  static constexpr uint32_t kDefaultMapFlags = MX_VM_FLAG_PERM_READ;

  explicit BufferConsumer(uint32_t map_flags = kDefaultMapFlags);
  ~BufferConsumer();

  // Gets the flags used for mapping VMOs.
  uint32_t map_flags() const { return map_flags_; }

  // Consumes the buffer, returning a holder which contains its associated
  // VMO and fence.
  // Returns nullptr if the buffer is null or cannot be consumed.
  std::unique_ptr<ConsumedBufferHolder> ConsumeBuffer(BufferPtr buffer);

 private:
  // |mtl::MessageLoopHandler|
  void OnHandleReady(mx_handle_t handle,
                     mx_signals_t pending,
                     uint64_t count) override;
  void OnHandleError(mx_handle_t handle, mx_status_t error) override;

  void TracePooledBufferCount() const;

  uint32_t const map_flags_;

  // Using shared_ptr because we need thread-safe weak references to it.
  std::shared_ptr<ConsumedBufferRegistry> registry_;

  struct RetentionInfo {
    mtl::MessageLoop::HandlerKey handler_key;
    ftl::RefPtr<mtl::SharedVmo> shared_vmo;
    mx_koid_t retention_koid;
  };
  std::unordered_map<mx_handle_t, RetentionInfo> retained_buffers_;

  FTL_DISALLOW_COPY_AND_ASSIGN(BufferConsumer);
};

// Holds a buffer and its fence for consumption.
class ConsumedBufferHolder {
 public:
  // Releases the consumed buffer, closing the fence and implicitly signalling
  // the producer that the buffer is available to be recycled.
  ~ConsumedBufferHolder();

  // Gets the shared VMO which backs this buffer.  Never nullptr.
  const ftl::RefPtr<mtl::SharedVmo>& shared_vmo() const { return shared_vmo_; }

  // Gets the buffer's fence.
  // Returns nullptr if the buffer does not have a fence.
  BufferFence* fence() const { return fence_.get(); }

  // Takes ownership of the buffer's fence.
  // Returns nullptr if the buffer does not have a fence.
  std::unique_ptr<BufferFence> TakeFence() { return std::move(fence_); }

 private:
  explicit ConsumedBufferHolder(ftl::RefPtr<mtl::SharedVmo> shared_vmo,
                                std::unique_ptr<BufferFence> fence);

  ftl::RefPtr<mtl::SharedVmo> shared_vmo_;
  std::unique_ptr<BufferFence> fence_;

  friend class BufferConsumer;
  FTL_DISALLOW_COPY_AND_ASSIGN(ConsumedBufferHolder);
};

}  // namespace mozart

#endif  // APPS_MOZART_SERVICES_BUFFERS_CPP_BUFFER_CONSUMER_H_
