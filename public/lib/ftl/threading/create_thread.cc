// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/threading/create_thread.h"

#include <utility>

#include "lib/ftl/tasks/incoming_task_queue.h"
#include "lib/ftl/tasks/message_loop.h"

namespace ftl {
namespace {

void RunMessageLoop(RefPtr<internal::IncomingTaskQueue> task_queue) {
  MessageLoop message_loop(std::move(task_queue));
  message_loop.Run();
}

}  // namespace

std::thread CreateThread(RefPtr<TaskRunner>* task_runner) {
  auto incoming_queue = MakeRefCounted<internal::IncomingTaskQueue>();
  *task_runner = incoming_queue;
  return std::thread(RunMessageLoop, std::move(incoming_queue));
}

}  // namespace ftl
