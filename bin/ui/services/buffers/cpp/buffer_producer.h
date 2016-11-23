// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SERVICES_BUFFERS_CPP_BUFFER_PRODUCER_H_
#define APPS_MOZART_SERVICES_BUFFERS_CPP_BUFFER_PRODUCER_H_

#include <mx/eventpair.h>

#include <memory>
#include <set>
#include <unordered_map>

#include "apps/mozart/services/buffers/buffer.fidl.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/tasks/message_loop_handler.h"
#include "lib/mtl/vmo/shared_vmo.h"

namespace mozart {

class ProducedBufferHolder;

// Maintains a pool of buffers which can be recycled as they are released
// by the consumer.
//
// This object is bound to the current message loop thread and can only
// be used on that thread.
class BufferProducer : private mtl::MessageLoopHandler {
 public:
  static constexpr uint32_t kDefaultMapFlags =
      MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE;

  explicit BufferProducer(uint32_t map_flags = kDefaultMapFlags);
  ~BufferProducer();

  // Gets the flags used for mapping VMOs.
  uint32_t map_flags() const { return map_flags_; }

  // Produces a new buffer.
  // Returns nullptr if the buffer cannot be produced.
  std::unique_ptr<ProducedBufferHolder> ProduceBuffer(size_t size);

  // Notifies the buffer producer that a cycle has completed (e.g., an entire
  // frame has been produced). The buffer producer will use this signal as a
  // time scale for pruning its internal cache.
  void Tick();

 private:
  // |mtl::MessageLoopHandler|
  void OnHandleReady(mx_handle_t handle, mx_signals_t pending) override;

  ftl::RefPtr<mtl::SharedVmo> GetSharedVmo(size_t size);
  ftl::RefPtr<mtl::SharedVmo> CreateSharedVmo(size_t size);

  uint32_t const map_flags_;

  struct FlightInfo {
    mtl::MessageLoop::HandlerKey handler_key;
    ftl::RefPtr<mtl::SharedVmo> shared_vmo;
    std::shared_ptr<mx::eventpair> production_fence;
  };

  struct BufferInfo {
    BufferInfo(uint32_t tick_count, ftl::RefPtr<mtl::SharedVmo> vmo)
        : tick_count(tick_count), vmo(std::move(vmo)) {}

    uint32_t tick_count;
    ftl::RefPtr<mtl::SharedVmo> vmo;
  };

  struct CompareBufferInfo {
    bool operator()(const std::unique_ptr<BufferInfo>& a,
                    const std::unique_ptr<BufferInfo>& b) {
      return a->vmo->vmo_size() < b->vmo->vmo_size();
    }
  };

  std::unordered_map<mx_handle_t, FlightInfo> buffers_in_flight_;
  std::multiset<std::unique_ptr<BufferInfo>, CompareBufferInfo>
      available_buffers_;

  FTL_DISALLOW_COPY_AND_ASSIGN(BufferProducer);
};

// Holds a buffer and its fence for production.
//
// To ensure that buffers are recycled, do not hold references to this object
// once production is finished and the buffer has been marked as ready.
class ProducedBufferHolder {
 public:
  static constexpr uint32_t kDefaultVmoRights =
      MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_MAP;

  // Releases the produced buffer.
  // Implicitly calls |SetReadySignal| to indicate to consumers that the
  // buffer is ready to be consumed.
  ~ProducedBufferHolder();

  // Gets the shared VMO which backs this buffer.  Never null.
  const ftl::RefPtr<mtl::SharedVmo>& shared_vmo() const { return shared_vmo_; }

  // Returns true if |SetReadySignal| was called.
  bool is_ready() const { return ready_; }

  // Signals the fence to indicate that the buffer is ready to be consumed.
  void SetReadySignal();

  // Gets a |Buffer| object to be transferred to a consumer, taking
  // care to assign only the specified |vmo_rights| to the buffer's VMO.
  // Any number of buffers may be produced from the same data.
  // Returns nullptr if the buffer cannot be obtained.
  BufferPtr GetBuffer(uint32_t vmo_rights = kDefaultVmoRights);

 private:
  explicit ProducedBufferHolder(ftl::RefPtr<mtl::SharedVmo> shared_vmo,
                                std::shared_ptr<mx::eventpair> production_fence,
                                mx::eventpair consumption_fence);

  ftl::RefPtr<mtl::SharedVmo> const shared_vmo_;

  std::shared_ptr<mx::eventpair> production_fence_;
  mx::eventpair consumption_fence_;
  bool ready_ = false;

  friend class BufferProducer;
  FTL_DISALLOW_COPY_AND_ASSIGN(ProducedBufferHolder);
};

}  // namespace mozart

#endif  // APPS_MOZART_SERVICES_BUFFERS_CPP_BUFFER_PRODUCER_H_
