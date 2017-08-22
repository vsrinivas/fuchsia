// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <queue>
#include <unordered_map>
#include <vector>

#include "apps/media/src/framework/refs.h"
#include "apps/media/src/framework/stages/stage.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/synchronization/mutex.h"
#include "lib/ftl/synchronization/thread_annotations.h"

namespace media {

//
// DESIGN
//
// |Engine| uses a 'work list' algorithm to operate the graph. The
// engine has a backlog of stages that need to be updated. To advance the
// operation of the graph, the engine removes a stage from the backlog and calls
// the stage's |UpdateUntilDone| method. The |Stage::UpdateUntilDone| call may
// cause stages to be added synchronously to the the backlog. This procedure
// continues until the backlog is empty.
//
// Stage::Update is the stage's opportunity to react to the supply of new media
// via its inputs and the signalling of new demand via its outputs. During
// Update, the stage does whatever work it can with the current supply and
// demand, possibly supplying media downstream through its outputs and/or
// signalling new demand via its inputs. When a stage supplies media through
// an output, the downstream stage is added to the backlog. When a stage updates
// its demand through an input, the upstream stage is added to the backlog.
//
// The process starts when a stage calls its |NeedsUpdate| method due to an
// external event. Stages that implement synchronous models never do this. Other
// stages do this as directed by the nodes they host in accordance with their
// respective models. When a stage is ready to supply media or update demand
// due to external events, it calls NeedsUpdate. The engine responds by
// adding the stage to the backlog and then calling the update callback. The
// recipient of the callback then calls |Engine::UpdateOne| or
// |Engine::UpdateUntilDone| to update a single stage or burn down the backlog
// completely. The former is used for multi-proc dispatching. The latter is
// used for single proc. When |UpdateUntilDone| is used, the initiating
// stage is updated first, and then all the work that can be
// done synchronously as a result of the external event is completed. In this
// way, the operation of the graph is driven by external events signalled
// through update callbacks.
//
// This arrangement implies the following constraints:
//
// 1) A stage cannot update supply/demand on its inputs/outputs except during
//    Update. When an external event occurs, the stage and/or its hosted node
//    should update its internal state as required and call NeedsUpdate.
//    During the subsequent Update, the stage and/or node can then update
//    supply and/or demand.
// 2) Nodes cannot rely on being called back on the same thread on which they
//    invoke update callbacks. This may require additional synchronization and
//    thread transitions inside the node.
// 3) If a node takes a lock of its own during Update, it should not also hold
//    that lock when calling the update callback. Doing so may result in
//    deadlock.
//
// NOTE: Allocators, not otherwise discussed here, are required to be thread-
// safe so that packets may be cleaned up on any thread.
//

// Manages operation of a Graph.
class Engine {
 public:
  Engine();

  ~Engine();

  // Sets the update callback. The update callback can be called on any thread.
  void SetUpdateCallback(ftl::Closure update_callback) {
    update_callback_ = update_callback;
  }

  // Prepares the input and the subgraph upstream of it.
  void PrepareInput(Input* input);

  // Unprepares the input and the subgraph upstream of it.
  void UnprepareInput(Input* input);

  // Flushes the output and the subgraph downstream of it. |hold_frame|
  // indicates whether a video renderer should hold and display the newest
  // frame.
  void FlushOutput(Output* output, bool hold_frame);

  // Called to indicate that the specified stage needs to be updated.
  void StageNeedsUpdate(Stage* stage);

  // Updates one stage from the update backlog and returns true if the backlog
  // isn't empty. If the backlog is empty, returns false.
  bool UpdateOne();

  // Updates stages from the update backlog until the backlog is empty.
  void UpdateUntilDone();

 private:
  using UpstreamVisitor =
      std::function<void(Input* input,
                         Output* output,
                         const Stage::UpstreamCallback& callback)>;
  using DownstreamVisitor =
      std::function<void(Output* output,
                         Input* input,
                         const Stage::DownstreamCallback& callback)>;

  void VisitUpstream(Input* input, const UpstreamVisitor& visitor)
      FTL_LOCKS_EXCLUDED(backlog_mutex_);

  void VisitDownstream(Output* output, const DownstreamVisitor& visitor)
      FTL_LOCKS_EXCLUDED(backlog_mutex_);

  // Pushes the stage to the update backlog and returns an indication of whether
  // the update callback should be called.
  bool PushToUpdateBacklog(Stage* stage) FTL_LOCKS_EXCLUDED(backlog_mutex_);

  // Pops a stage from the update backlog and returns it or returns nullptr if
  // the update backlog is empty.
  Stage* PopFromUpdateBacklog() FTL_LOCKS_EXCLUDED(backlog_mutex_);

  ftl::Closure update_callback_;

  mutable ftl::Mutex backlog_mutex_;
  std::queue<Stage*> update_backlog_ FTL_GUARDED_BY(backlog_mutex_);
  bool suppress_update_callbacks_ FTL_GUARDED_BY(backlog_mutex_) = false;
};

}  // namespace media
