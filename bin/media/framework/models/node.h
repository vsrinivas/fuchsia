// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/src/framework/packet.h"
#include "apps/media/src/framework/payload_allocator.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/tasks/task_runner.h"

namespace media {

// Base class for all nodes.
template <typename TStage>
class Node {
 public:
  virtual ~Node() {}

  // Sets |stage_|. This method is called only by the graph.
  void SetStage(TStage* stage) {
    FTL_DCHECK(stage_ == nullptr);
    stage_ = stage;
  }

  // Returns the task runner to use for this node. The default implementation
  // returns nullptr, indicating that this node can use whatever task runner
  // is provided for it, either via the |Graph| constructor or via the
  // |Graph::Add| methods.
  virtual ftl::RefPtr<ftl::TaskRunner> GetTaskRunner() { return nullptr; }

 protected:
  // Returns a reference to the stage for this node.
  TStage& stage() {
    FTL_DCHECK(stage_);
    return *stage_;
  }

 private:
  TStage* stage_ = nullptr;

  friend class Graph;
};

}  // namespace media
