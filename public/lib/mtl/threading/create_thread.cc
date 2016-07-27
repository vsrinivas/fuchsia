// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/threading/create_thread.h"

#include <utility>

#include "lib/mtl/tasks/incoming_task_queue.h"
#include "lib/mtl/tasks/message_loop.h"

namespace mtl {
namespace {

void RunMessageLoop(ftl::RefPtr<internal::IncomingTaskQueue> task_queue) {
  MessageLoop message_loop(std::move(task_queue));
  message_loop.Run();
}

}  // namespace

std::thread CreateThread(ftl::RefPtr<ftl::TaskRunner>* task_runner) {
  auto incoming_queue = ftl::MakeRefCounted<internal::IncomingTaskQueue>();
  *task_runner = incoming_queue;
  return std::thread(RunMessageLoop, std::move(incoming_queue));
}

}  // namespace mtl
