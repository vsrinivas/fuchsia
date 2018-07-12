// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_STAGES_STAGE_IMPL_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_STAGES_STAGE_IMPL_H_

#include <atomic>
#include <mutex>
#include <queue>

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include "garnet/bin/media/media_player/framework/models/node.h"
#include "garnet/bin/media/media_player/framework/models/stage.h"
#include "garnet/bin/media/media_player/framework/packet.h"
#include "garnet/bin/media/media_player/framework/payload_allocator.h"
#include "garnet/bin/media/media_player/framework/stages/input.h"
#include "garnet/bin/media/media_player/framework/stages/output.h"
#include "lib/fxl/synchronization/thread_annotations.h"

namespace media_player {

// Host for a source, sink or transform.
//
// Flushing:
// A flushing operation starts at a given output and proceeds downstream until
// a sink (a stage with no outputs) is encountered. All FlushInput/Output calls
// in a given flush operation are issued without waiting for callbacks from the
// previous calls. The entire flush operation isn't complete until all the
// callbacks are called, at which time packet flow may resume or the graph may
// be edited.
class StageImpl : public std::enable_shared_from_this<StageImpl> {
 public:
  StageImpl();

  virtual ~StageImpl();

  // Called when the stage is shutting down. The default implementation does
  // nothing.
  virtual void OnShutDown();

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
  // by the output or nullptr if there is no such requirement.
  virtual void PrepareOutput(size_t index,
                             std::shared_ptr<PayloadAllocator> allocator) = 0;

  // Unprepares the input. The default implementation does nothing.
  virtual void UnprepareInput(size_t index);

  // Unprepares the output. The default implementation does nothing.
  virtual void UnprepareOutput(size_t index);

  // Flushes an input. |hold_frame| indicates whether a video renderer should
  // hold and display the newest frame. The callback is used to indicate that
  // the flush operation is complete. It must be called on the graph's thread
  // and may be called synchronously.
  //
  // The input in question must be flushed (|Input.Flush|) synchronously with
  // this call to eject the queued packet (if there is one) and clear the
  // input's need for a packet. The callback is provided in case the node
  // has additional flushing business that can't be completed synchronously.
  virtual void FlushInput(size_t index, bool hold_frame,
                          fit::closure callback) = 0;

  // Flushes an output. The callback is used to indicate that the flush
  // operation is complete. It must be called on the graph's thread and may be
  // called synchronously. The callback is provided in case the node has
  // additional flushing business that can't be completed synchronously.
  //
  // The output in question must not produce any packets after this method is
  // called and before the need for a packet is signalled.
  virtual void FlushOutput(size_t index, fit::closure callback) = 0;

  // Gets the generic node.
  virtual GenericNode* GetGenericNode() const = 0;

  // Shuts down the stage.
  void ShutDown();

  // Queues the stage for update if it isn't already queued. This method may
  // be called on any thread.
  void NeedsUpdate();

  // Calls |Update| until no more updates are required.
  void UpdateUntilDone();

  // Acquires the stage, preventing posted tasks from running until the stage
  // is released. |callback| is called when the stage is acquired.
  void Acquire(fit::closure callback);

  // Releases the stage previously acquired via |Acquire|.
  void Release();

  // Sets an |async_t| for running tasks .
  void SetDispatcher(async_dispatcher_t* dispatcher);

  void PostTask(fit::closure task);

 protected:
  // Updates packet supply and demand.
  virtual void Update() = 0;

  // Post a task that will run even if the stage has been shut down.
  void PostShutdownTask(fit::closure task);

 private:
  // Runs tasks in the task queue. This method is always called from
  // |dispatcher_|. A |StageImpl| funnels all task execution through
  // |RunTasks|. The lambdas that call |RunTasks| capture a shared pointer to
  // the stage, so the stage can't be deleted from the time such a lambda is
  // created until it's done executing |RunTasks|. A stage that's no longer
  // referenced by the graph will be deleted when all such lambdas have
  // completed. |ShutDown| prevents |RunTasks| from actually executing any
  // tasks.
  void RunTasks();

  async_dispatcher_t* dispatcher_;

  // Used for ensuring the stage is properly updated. This value is zero
  // initially, indicating that there's no need to update the stage. When the
  // stage needs updating, the counter is incremented. A transition from 0 to
  // 1 indicates that the stage should be enqueued. Before the update occurs,
  // this value is set to 1. If it's no longer 1 after update completes, it is
  // updated again. When an update completes and the counter is still 1, the
  // counter is reset to 0.
  std::atomic_uint32_t update_counter_;

  mutable std::mutex tasks_mutex_;
  // Pending tasks. Only |RunTasks| may pop from this queue.
  std::queue<fit::closure> tasks_ FXL_GUARDED_BY(tasks_mutex_);
  // Set to true to suspend task execution.
  bool tasks_suspended_ FXL_GUARDED_BY(tasks_mutex_) = false;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_STAGES_STAGE_IMPL_H_
