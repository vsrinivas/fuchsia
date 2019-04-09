// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_PAYLOADS_VMO_PAYLOAD_ALLOCATOR_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_PAYLOADS_VMO_PAYLOAD_ALLOCATOR_H_

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include <mutex>
#include <vector>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/playback/mediaplayer/graph/payloads/payload_allocator.h"
#include "src/media/playback/mediaplayer/graph/payloads/payload_config.h"

namespace media_player {

// Allocates |PayloadBuffers| from one or more VMOs.
//
// |VmoPayloadAllocator| supports all three valid |VmoAllocation| modes:
//     |kSingleVmo|    - There is only one VMO, and buffers are allocated from
//                       it.
//     |kVmoPerBuffer| - Each buffer occupies its own VMO.
//     |kUnrestricted| - There are one or more VMOs, and buffers are allocated
//                       from all of them.
//
// Nodes are free to use their own strategies to allocate from VMOs, in which
// case |VmoPayloadAllocator::AllocatePayloadBuffer| is never called. In the
// case of an output, the node can simply create |PayloadBuffers| that
// reference the VMOs when it needs them. In the case of an input, the node
// registers its allocation function when calling |ConfigureInputToUseVmos| or
// |ConfigureInputToProvideVmos|.
class VmoPayloadAllocator : public PayloadAllocator,
                            public PayloadVmoProvision,
                            public fbl::RefCounted<VmoPayloadAllocator> {
 public:
  static fbl::RefPtr<VmoPayloadAllocator> Create();

  VmoPayloadAllocator() = default;

  ~VmoPayloadAllocator() override = default;

  // Dumps this |VmoPayloadAllocator|'s state to |os|.
  void Dump(std::ostream& os) const;

  // Returns the current |VmoAllocation| configuration. This value is
  // |kNotApplicable| initially and must be set only once and before
  // |AllocatePayloadBuffer| is called.
  VmoAllocation vmo_allocation() const {
    std::lock_guard<std::mutex> locker(mutex_);
    return vmo_allocation_;
  }

  // Sets the current |VmoAllocation| configuration. Must be called before
  // |AllocatePayloadBuffer| is called. |vmo_allocation| may not be
  // |kNotApplicable|.
  void SetVmoAllocation(VmoAllocation vmo_allocation);

  // PayloadAllocator implementation.
  fbl::RefPtr<PayloadBuffer> AllocatePayloadBuffer(uint64_t size) override;

  // PayloadVmos implementation.
  std::vector<fbl::RefPtr<PayloadVmo>> GetVmos() const override;

  // PayloadVmoProvision implementation.
  void AddVmo(fbl::RefPtr<PayloadVmo> vmo) override;

  void RemoveVmo(fbl::RefPtr<PayloadVmo> payload_vmo) override;

  void RemoveAllVmos() override {
    std::lock_guard<std::mutex> locker(mutex_);
    payload_vmos_.clear();
  }

 private:
  // Attempts to allocate memory from the specified VMO.
  fbl::RefPtr<PayloadBuffer> TryAllocateFromVmo(
      fbl::RefPtr<PayloadVmo> payload_vmo, uint64_t size)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  mutable std::mutex mutex_;
  VmoAllocation vmo_allocation_ FXL_GUARDED_BY(mutex_) =
      VmoAllocation::kNotApplicable;
  std::vector<fbl::RefPtr<PayloadVmo>> payload_vmos_ FXL_GUARDED_BY(mutex_);
  size_t suggested_allocation_vmo_ FXL_GUARDED_BY(mutex_) = 0;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_PAYLOADS_VMO_PAYLOAD_ALLOCATOR_H_
