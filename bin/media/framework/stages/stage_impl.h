// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>
#include <queue>

#include "garnet/bin/media/framework/models/node.h"
#include "garnet/bin/media/framework/models/stage.h"
#include "garnet/bin/media/framework/packet.h"
#include "garnet/bin/media/framework/payload_allocator.h"
#include "garnet/bin/media/framework/stages/input.h"
#include "garnet/bin/media/framework/stages/output.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/synchronization/mutex.h"
#include "lib/fxl/synchronization/thread_annotations.h"

namespace media {

// Host for a source, sink or transform.
class StageImpl : public std::enable_shared_from_this<StageImpl> {
 public:
  using UpstreamCallback = std::function<void(size_t input_index)>;
  using DownstreamCallback = std::function<void(size_t output_index)>;

  StageImpl();

  virtual ~StageImpl();

  // Shuts down the stage prior to destruction.
  virtual void ShutDown();

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
  virtual std::shared_ptr<PayloadAllocator> PrepareInput(size_t index) = 0;

  // Prepares the output for operation, passing an allocator that must be used
  // by the output or nullptr if there is no such requirement. The callback is
  // used to indicate what inputs are ready to be prepared as a consequence of
  // preparing the output.
  virtual void PrepareOutput(size_t index,
                             std::shared_ptr<PayloadAllocator> allocator,
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

  // Acquires the stage, preventing posted tasks from running until the stage
  // is released. |callback| is called when the stage is acquired.
  void Acquire(const fxl::Closure& callback);

  // Releases the stage previously acquired via |Acquire|.
  void Release();

  // Sets a |TaskRunner| for running tasks relating to this stage and the node
  // it hosts. The stage ensures that only one task related to this stage runs
  // at any given time. Before using the provided |TaskRunner|, the stage
  // calls the node's |GetTaskRunner| method to determine if the node has a
  // |TaskRunner| it would prefer to use. If so, it uses that one instead of
  // |task_runner|.
  void SetTaskRunner(fxl::RefPtr<fxl::TaskRunner> task_runner);

  void PostTask(const fxl::Closure& task);

 protected:
  // Gets the generic node.
  virtual GenericNode* GetGenericNode() = 0;

  // Releases ownership of the node.
  virtual void ReleaseNode() = 0;

  // Updates packet supply and demand.
  virtual void Update() = 0;

  // Post a task that will run even if the stage has been shut down.
  void PostShutdownTask(fxl::Closure task);

 private:
  // Runs tasks in the task queue. This method is always called from
  // |task_runner_|. A |StageImpl| funnels all task execution through
  // |RunTasks|. The lambdas that call |RunTasks| capture a shared pointer to
  // the stage, so the stage can't be deleted from the time such a lambda is
  // created until it's done executing |RunTasks|. A stage that's no longer
  // referenced by the graph will be deleted when all such lambdas have
  // completed. |ShutDown| prevents |RunTasks| from actually executing any
  // tasks.
  void RunTasks();

  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  // Used for ensuring the stage is properly updated. This value is zero
  // initially, indicating that there's no need to update the stage. When the
  // stage needs updating, the counter is incremented. A transition from 0 to
  // 1 indicates that the stage should be enqueued. Before the update occurs,
  // this value is set to 1. If it's no longer 1 after update completes, it is
  // updated again. When an update completes and the counter is still 1, the
  // counter is reset to 0.
  std::atomic_uint32_t update_counter_;

  mutable fxl::Mutex tasks_mutex_;
  // Pending tasks. Only |RunTasks| may pop from this queue.
  std::queue<fxl::Closure> tasks_ FXL_GUARDED_BY(tasks_mutex_);
  // Set to true to suspend task execution.
  bool tasks_suspended_ FXL_GUARDED_BY(tasks_mutex_) = false;
};

}  // namespace media
