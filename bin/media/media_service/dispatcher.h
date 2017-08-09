// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <thread>
#include <vector>

#include <mx/port.h>

#include "apps/media/src/framework/graph.h"
#include "lib/ftl/macros.h"

namespace media {

// Dispatches graph updates to multiple processes.
class Dispatcher {
 public:
  Dispatcher(uint32_t thread_count);

  ~Dispatcher();

  void PostUpdate(Graph* graph);

 private:
  void Worker(uint32_t thread_number);

  void QueuePacket(uint64_t key, void* payload = nullptr);

  mx::port port_;
  std::vector<std::thread> threads_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Dispatcher);
};

}  // namespace media
