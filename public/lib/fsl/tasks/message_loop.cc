// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/tasks/message_loop.h"

#include <utility>

#include <lib/async/cpp/task.h>
#include <zircon/syscalls.h>

#include "lib/fxl/logging.h"

namespace fsl {
namespace {

thread_local MessageLoop* g_current;

}  // namespace

MessageLoop::MessageLoop()
    : MessageLoop(fxl::MakeRefCounted<internal::IncomingTaskQueue>()) {}

MessageLoop::MessageLoop(
    fxl::RefPtr<internal::IncomingTaskQueue> incoming_tasks)
    : loop_config_{.make_default_for_current_thread = true,
                   .epilogue = &MessageLoop::Epilogue,
                   .data = this},
      loop_(&loop_config_),
      task_runner_(std::move(incoming_tasks)) {
  FXL_DCHECK(!g_current) << "At most one message loop per thread.";
  g_current = this;

  MessageLoop::incoming_tasks()->InitDelegate(this);
}

MessageLoop::~MessageLoop() {
  FXL_DCHECK(g_current == this)
      << "Message loops must be destroyed on their own threads.";

  loop_.Shutdown();

  incoming_tasks()->ClearDelegate();

  g_current = nullptr;
}

MessageLoop* MessageLoop::GetCurrent() { return g_current; }

void MessageLoop::PostTask(fxl::Closure task, fxl::TimePoint target_time) {
  zx_status_t status = async::PostTaskForTime(
      loop_.async(), [task = std::move(task)] { task(); },
      zx::time(target_time.ToEpochDelta().ToNanoseconds()));
  FXL_CHECK(status == ZX_OK || status == ZX_ERR_BAD_STATE)
      << "Failed to post task: status=" << status;
}

void MessageLoop::Run(bool until_idle) {
  FXL_DCHECK(g_current == this);

  FXL_CHECK(!is_running_) << "Cannot run a nested message loop.";
  is_running_ = true;

  zx_status_t status = until_idle ? loop_.RunUntilIdle() : loop_.Run();
  FXL_CHECK(status == ZX_OK || status == ZX_ERR_CANCELED)
      << "Loop stopped abnormally: status=" << status;

  status = loop_.ResetQuit();
  FXL_DCHECK(status == ZX_OK)
      << "Failed to reset quit state: status=" << status;

  FXL_DCHECK(is_running_);
  is_running_ = false;
}

void MessageLoop::Run() { Run(false); }

void MessageLoop::RunUntilIdle() { Run(true); }

void MessageLoop::QuitNow() {
  FXL_DCHECK(g_current == this);

  if (is_running_)
    loop_.Quit();
}

void MessageLoop::PostQuitTask() {
  task_runner()->PostTask([this]() { QuitNow(); });
}

bool MessageLoop::RunsTasksOnCurrentThread() { return g_current == this; }

void MessageLoop::SetAfterTaskCallback(fxl::Closure callback) {
  FXL_DCHECK(g_current == this);

  after_task_callback_ = std::move(callback);
}

void MessageLoop::ClearAfterTaskCallback() {
  FXL_DCHECK(g_current == this);

  after_task_callback_ = fxl::Closure();
}

void MessageLoop::Epilogue(async_loop_t*, void* data) {
  auto loop = static_cast<MessageLoop*>(data);
  if (loop->after_task_callback_)
    loop->after_task_callback_();
}

}  // namespace fsl
