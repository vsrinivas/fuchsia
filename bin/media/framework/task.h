// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <vector>

#include "lib/ftl/functional/closure.h"

#pragma once

namespace media {

class Engine;
class Stage;

// Represents a task to be executed against an arbitrary number of stages
// exclusive of updates and other tasks.
class Task {
 public:
  // Constructs a task. |function| is executed on an arbitrary thread once
  // |Unblock| is called and all the indicated stages have been acquired.
  Task(Engine* engine,
       const ftl::Closure& function,
       std::vector<Stage*> stages);

  ~Task();

  // Indicates that one of the required stages has been acquired.
  void Unblock();

  // Indicates that one of the required stages has been acquired.
  void StageAcquired();

 private:
  Engine* engine_;
  ftl::Closure function_;
  std::vector<Stage*> stages_;
  std::atomic_uint32_t unacquired_stage_count_;
};

}  // namespace media
