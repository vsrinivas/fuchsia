// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_COMPONENT_CONTEXT_H_
#define SRC_SYS_FUZZING_COMMON_COMPONENT_CONTEXT_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-types.h"

namespace fuzzing {

// This class is a wrapper around |sys::ComponentContext| that provides some additional common
// behaviors, such as making an |async::Loop| and scheduling a primary task on an |async::Executor|.
class ComponentContext final {
 public:
  // By default, run on the current thread.
  ComponentContext() : ComponentContext(/* use_thread */ false) {}

  explicit ComponentContext(bool use_thread) : use_thread_(use_thread) {
    if (use_thread_) {
      loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    } else {
      loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
    }
    auto context = sys::ComponentContext::Create();
    svc_ = context->svc();
    outgoing_ = context->outgoing();
    executor_ = MakeExecutor(loop_->dispatcher());
  }

  ~ComponentContext() {
    if (use_thread_) {
      loop_->Shutdown();
      loop_->JoinThreads();
    }
  }

  const ExecutorPtr& executor() const { return executor_; }

  // Adds an interface request handler for a protocol capability provided by this component.
  template <typename Interface>
  void AddPublicService(fidl::InterfaceRequestHandler<Interface> handler) const {
    auto status = outgoing_->AddPublicService(std::move(handler));
    FX_CHECK(status == ZX_OK) << zx_status_get_string(status);
  }

  // Returns a handler to connect |request|s to a protocol capability provided by another component.
  template <typename Interface>
  fidl::InterfaceRequestHandler<Interface> MakeRequestHandler() {
    return [svc = svc_](fidl::InterfaceRequest<Interface> request) {
      svc->Connect(std::move(request));
    };
  }

  // Schedules a task to be executed when |Run| is invoked.
  template <typename Task>
  void ScheduleTask(Task task) {
    executor_->schedule_task(std::move(task));
  }

  // Runs the message loop on the current thread. This method should be called exactly once.
  zx_status_t Run() {
    outgoing_->ServeFromStartupInfo(loop_->dispatcher());
    return use_thread_ ? loop_->StartThread() : loop_->Run();
  }

 private:
  bool use_thread_;
  std::unique_ptr<async::Loop> loop_;
  std::shared_ptr<sys::ServiceDirectory> svc_;
  std::shared_ptr<sys::OutgoingDirectory> outgoing_;
  ExecutorPtr executor_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(ComponentContext);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_COMPONENT_CONTEXT_H_
