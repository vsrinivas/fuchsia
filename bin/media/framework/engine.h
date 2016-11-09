// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <list>
#include <queue>
#include <stack>
#include <unordered_map>

#include "apps/media/src/framework/refs.h"
#include "apps/media/src/framework/stages/stage.h"
#include "lib/ftl/synchronization/mutex.h"
#include "lib/ftl/synchronization/thread_annotations.h"

namespace media {

//
// DESIGN
//
// Engine uses a 'work list' algorithm to operate the graph. The
// engine has a backlog of stages that need to be updated. To advance the
// operation of the graph, the engine removes a stage from the backlog and calls
// the stage's Update method. The Stage::Update may cause stages to be added
// synchronously to the the backlog. This procedure continues until the backlog
// is empty.
//
// Stage::Update is the stage's opportunity to react to the supply of new media
// via its inputs and the signalling of new demand via its outputs. During
// Update, the stage does whatever work it can with the current supply and
// demand, possibly supplying media downstream through its outputs and/or
// signalling new demand via its inputs. When a stage supplies media through
// an output, the downstream stage is added to the backlog. When a stage updates
// its demand through an input, the upstream stage is added to the backlog.
//
// The process starts when a stage invokes an update callback supplied by the
// engine. Stages that implement synchronous models never do this. Other stages
// do this as directed by the parts they host in accordance with their
// respective models. When a stage is ready to supply media or update demand
// due to external events, it calls the update callback. The engine responds by
// adding the stage to the backlog and then burning down the backlog. The stage
// that called back is updated first, and then all the work that can be done
// synchronously as a result of the external event is completed. In this way,
// the operation of the graph is driven by external events signalled through
// update callbacks.
//
// Currently, Engine uses an opportunistic threading model that only allows
// one thread to drive the backlog processing at any given time. The engine
// runs the processing on whatever thread enters it via an update callback.
// An engine employs a single lock that protects manipulation of the graph and
// processing of the backlog. Stage update methods are invoked with that lock
// taken. This arrangement implies the following constraints:
//
// 1) An update callback cannot be called synchronously with a Stage::Update
//    call, because the lock is taken for the duration of Update, and the
//    callback will take the lock.
// 2) A stage cannot update supply/demand on its inputs/outputs except during
//    Update. When an external event occurs, the stage and/or its hosted part
//    should update its internal state as required and invoke the callback.
//    During the subsequent Update, the stage and/or part can then update
//    supply and/or demand.
// 3) Threads used to call update callbacks must be suitable for operating the
//    engine. There is currently no affordance for processing other tasks on
//    the thread while the callback is running. A callback may run for a long
//    time, depending on how much work needs to be done.
// 4) Parts cannot rely on being called back on the same thread on which they
//    invoke update callbacks. This may require additional synchronization and
//    thread transitions inside the part.
// 5) If a part takes a lock of its own during Update, it should not also hold
//    that lock when calling the update callback. Doing so will result in
//    deadlock.
//
// NOTE: Allocators, not otherwise discussed here, are required to be thread-
// safe so that packets may be cleaned up on any thread.
//
// In the future, the threading model will be enhanced. Intended features
// include:
// 1) Support for multiple threads.
// 2) Marshalling update callbacks to a different thread.
//

// Manages operation of a Graph.
class Engine {
 public:
  Engine();

  ~Engine();

  // Prepares the input and the subgraph upstream of it.
  void PrepareInput(const InputRef& input_ref);

  // Unprepares the input and the subgraph upstream of it.
  void UnprepareInput(const InputRef& input_ref);

  // Flushes the output and the subgraph downstream of it.
  void FlushOutput(const OutputRef& output_ref);

  // Queues the stage for update and winds down the backlog.
  void RequestUpdate(Stage* stage);

  // Pushes the stage to the supply backlog if it isn't already there.
  void PushToSupplyBacklog(Stage* stage);

  // Pushes the stage to the demand backlog if it isn't already there.
  void PushToDemandBacklog(Stage* stage);

 private:
  using UpstreamVisitor =
      std::function<void(const InputRef& input,
                         const OutputRef& output,
                         const Stage::UpstreamCallback& callback)>;
  using DownstreamVisitor =
      std::function<void(const OutputRef& output,
                         const InputRef& input,
                         const Stage::DownstreamCallback& callback)>;

  void VisitUpstream(const InputRef& input, const UpstreamVisitor& vistor);

  void VisitDownstream(const OutputRef& output,
                       const DownstreamVisitor& vistor);

  // Processes the entire backlog.
  void Update();

  // Performs processing for a single stage, updating the backlog accordingly.
  void Update(Stage* stage);

  // Pops a stage from the supply backlog and returns it or returns nullptr if
  // the supply backlog is empty.
  Stage* PopFromSupplyBacklog();

  // Pops a stage from the demand backlog and returns it or returns nullptr if
  // the demand backlog is empty.
  Stage* PopFromDemandBacklog();

  mutable ftl::Mutex mutex_;
  // supply_backlog_ contains pointers to all the stages that have been supplied
  // (packets or frames) but have not been updated since. demand_backlog_ does
  // the same for demand. The use of queue vs stack here is a guess as to what
  // will yield the best results. It's possible that only a single backlog is
  // required.
  // TODO(dalesat): Determine the best ordering and implement it.
  std::queue<Stage*> supply_backlog_ FTL_GUARDED_BY(mutex_);
  std::stack<Stage*> demand_backlog_ FTL_GUARDED_BY(mutex_);
  bool packets_produced_ FTL_GUARDED_BY(mutex_);
};

}  // namespace media
