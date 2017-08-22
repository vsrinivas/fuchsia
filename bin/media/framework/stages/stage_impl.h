// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>
#include <queue>

#include "apps/media/src/framework/models/stage.h"
#include "apps/media/src/framework/packet.h"
#include "apps/media/src/framework/payload_allocator.h"
#include "apps/media/src/framework/stages/input.h"
#include "apps/media/src/framework/stages/output.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/synchronization/mutex.h"
#include "lib/ftl/synchronization/thread_annotations.h"

namespace media {

class Engine;

// Host for a source, sink or transform.
class StageImpl {
 public:
  using UpstreamCallback = std::function<void(size_t input_index)>;
  using DownstreamCallback = std::function<void(size_t output_index)>;

  StageImpl(Engine* engine);

  virtual ~StageImpl();

  // Returns the number of input connections.
  virtual size_t input_count() const = 0;

  // Returns the indicated input connection.
  virtual Input& input(size_t index) = 0;

  // Returns the number of output connections.
  virtual size_t output_count() const = 0;

  // Returns the indicated output connection.
  virtual Output& output(size_t index) = 0;

  // Prepares the input for operation. Returns nullptr unless the connected
  // output must use a specific allocator, in which case it returns that
  // allocator.
  virtual PayloadAllocator* PrepareInput(size_t index) = 0;

  // Prepares the output for operation, passing an allocator that must be used
  // by the output or nullptr if there is no such requirement. The callback is
  // used to indicate what inputs are ready to be prepared as a consequence of
  // preparing the output.
  virtual void PrepareOutput(size_t index,
                             PayloadAllocator* allocator,
                             const UpstreamCallback& callback) = 0;

  // Unprepares the input. The default implementation does nothing.
  virtual void UnprepareInput(size_t index);

  // Unprepares the output. The default implementation does nothing. The
  // the callback is used to indicate what inputs are ready to be unprepared as
  // a consequence of unpreparing the output.
  virtual void UnprepareOutput(size_t index, const UpstreamCallback& callback);

  // Flushes an input. |hold_frame| indicates whether a video renderer should
  // hold and display the newest frame. The callback is used to indicate what
  // outputs are ready to be flushed as a consequence of flushing the input.
  virtual void FlushInput(size_t index,
                          bool hold_frame,
                          const DownstreamCallback& callback) = 0;

  // Flushes an output.
  virtual void FlushOutput(size_t index) = 0;

  // Queues the stage for update if it isn't already queued. This method may
  // be called on any thread.
  void NeedsUpdate();

  // Calls |Update| until no more updates are required.
  void UpdateUntilDone();

 protected:
  // Updates packet supply and demand.
  virtual void Update() = 0;

 private:
  Engine* const engine_;

  // Used for ensuring the stage is properly updated. This value is zero
  // initially, indicating that there's no need to update the stage. When the
  // stage needs updating, the counter is incremented. A transition from 0 to
  // 1 indicates that the stage should be enqueued. Before the update occurs,
  // this value is set to 1. If it's no longer 1 after update completes, it is
  // updated again. When an update completes and the counter is still 1, the
  // counter is reset to 0.
  std::atomic_uint32_t update_counter_;

  friend class Engine;
};

}  // namespace media
