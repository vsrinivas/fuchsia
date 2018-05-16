// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_PLAYER_SEGMENT_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_PLAYER_SEGMENT_H_

#include <media/cpp/fidl.h>
#include <media_player/cpp/fidl.h>
#include <lib/async/dispatcher.h>

#include "garnet/bin/media/media_player/framework/graph.h"
#include "garnet/bin/media/media_player/framework/metadata.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/logging.h"

namespace media_player {

// A graph segment.
//
// A graph segment is initially unprovisioned, meaning that the |graph| and
// |async| methods may not be called, and |provisioned| returns false.
// When it's provisioned, the |DidProvision| method is called, at which time
// the |graph| and |async| methods are valid to call, and |provisioned|
// returns true. Before the segment is deprovisioned, the |WillDeprovision|
// method is called.
class Segment {
 public:
  Segment();

  virtual ~Segment();

  // Provides the graph and task runner for this segment. |update_callback|
  // is called whenever the player should reinterrogate the segment for state
  // changes. The update callback is used to notify of changes to the value
  // returned by problem(). Subclasses of Segment may use this callback to
  // signal additional changes.
  void Provision(Graph* graph, async_t* async, fxl::Closure update_callback);

  // Revokes the graph, task runner and update callback provided in a previous
  // call to |Provision|.
  virtual void Deprovision();

  // Returns the current problem preventing intended operation or nullptr if
  // there is no such problem.
  const Problem* problem() const { return problem_.get(); }

 protected:
  Graph& graph() {
    FXL_DCHECK(graph_) << "graph() called on unprovisioned segment.";
    return *graph_;
  }

  async_t* async() {
    FXL_DCHECK(async_) << "async() called on unprovisioned segment.";
    return async_;
  }

  // Notifies the player of state updates (calls the update callback).
  void NotifyUpdate();

  // Report a problem.
  void ReportProblem(const std::string& type, const std::string& details);

  // Clear any prior problem report.
  void ReportNoProblem();

  // Indicates whether the segment is provisioned.
  bool provisioned() { return graph_ != nullptr; }

  // Called when the segment has been provisioned. The default implementation
  // does nothing.
  virtual void DidProvision() {}

  // Called when the segment is about to be deprovisioned. The default
  // implementation does nothing.
  virtual void WillDeprovision() {}

 private:
  Graph* graph_ = nullptr;
  async_t* async_ = nullptr;
  fxl::Closure update_callback_;
  ProblemPtr problem_;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_PLAYER_SEGMENT_H_
