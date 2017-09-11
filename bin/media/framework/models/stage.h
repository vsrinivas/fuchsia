// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fxl/tasks/task_runner.h"

namespace media {

// Host for node, from the perspective of the node.
class Stage {
 public:
  virtual ~Stage() {}

  // Sets a |TaskRunner| for running tasks relating to this stage and the node
  // it hosts. The stage ensures that only one task related to this stage runs
  // at any given time. Before using the provided |TaskRunner|, the stage
  // calls the node's |GetTaskRunner| method to determine if the node has a
  // |TaskRunner| it would prefer to use. If so, it uses that one instead of
  // |task_runner|.
  virtual void SetTaskRunner(fxl::RefPtr<fxl::TaskRunner> task_runner) = 0;

  // Posts a task to run as soon as possible. A Task posted with this method is
  // run exclusive of any other such tasks.
  virtual void PostTask(const fxl::Closure& task) = 0;
};

}  // namespace media
