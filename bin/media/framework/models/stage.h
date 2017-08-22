// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/ftl/tasks/task_runner.h"

namespace media {

// Host for node, from the perspective of the node.
class Stage {
 public:
  virtual ~Stage() {}
};

}  // namespace media
