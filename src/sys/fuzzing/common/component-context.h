// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_COMPONENT_CONTEXT_H_
#define SRC_SYS_FUZZING_COMMON_COMPONENT_CONTEXT_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/outgoing_directory.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <memory>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-types.h"

namespace fuzzing {

// Aliases to simplify passing around the unique context.
class ComponentContext;
using ComponentContextPtr = std::unique_ptr<ComponentContext>;

// This class is a wrapper around |sys::ComponentContext| that provides some additional common
// behaviors, such as making an |async::Loop| and scheduling a primary task on an |async::Executor|.
class ComponentContext {
 public:
  // Creates a component context. This method consumes startup handles in order to serve FIDL
  // protocols, and can therefore be called at most once per process.
  static ComponentContextPtr Create();

  // Creates an "auxiliary" context that does not have an outgoing directory. Such a context can
  // only be used for creating FIDL clients, but does not consume any startup handles and thus does
  // not preclude creating other component contexts.
  static ComponentContextPtr CreateAuxillary();

  ComponentContext() = default;
  virtual ~ComponentContext();

  const ExecutorPtr& executor() const { return executor_; }

  // These must match the channel IDs used by fuzz_test_runner.
  static constexpr uint32_t kRegistrarId = 0;
  static constexpr uint32_t kCoverageId = 1;

  // Takes the |PA_HND(PA_USER0, arg)| startup handle.
  virtual zx::channel TakeChannel(uint32_t arg);

  // Adds an interface request handler for a protocol capability provided by this component.
  template <typename Interface>
  __WARN_UNUSED_RESULT zx_status_t
  AddPublicService(fidl::InterfaceRequestHandler<Interface> handler) const {
    return outgoing_->AddPublicService(std::move(handler));
  }

  // Connects a |request| to a protocol capability provided by another component.
  template <typename Interface>
  __WARN_UNUSED_RESULT zx_status_t Connect(fidl::InterfaceRequest<Interface> request) {
    return Connect(svc_, std::move(request));
  }

  // Returns a handler to connect |request|s to a protocol capability provided by another component.
  template <typename Interface>
  fidl::InterfaceRequestHandler<Interface> MakeRequestHandler() {
    return [svc = svc_](fidl::InterfaceRequest<Interface> request) {
      Connect(svc, std::move(request));
    };
  }

  // Schedules a task to be executed when |Run| is invoked.
  template <typename Task>
  void ScheduleTask(Task task) {
    executor_->schedule_task(std::move(task));
  }

  // Runs the message loop on the current thread. This method should only be called at most once.
  __WARN_UNUSED_RESULT virtual zx_status_t Run();

  // Runs until there are no tasks that can make progress.
  __WARN_UNUSED_RESULT virtual zx_status_t RunUntilIdle();

 protected:
  using LoopPtr = std::unique_ptr<async::Loop>;
  using ServiceDirectoryPtr = std::shared_ptr<sys::ServiceDirectory>;
  using OutgoingDirectoryPtr = std::shared_ptr<sys::OutgoingDirectory>;

  void set_executor(ExecutorPtr executor) { executor_ = std::move(executor); }
  void set_svc(ServiceDirectoryPtr svc) { svc_ = std::move(svc); }

 private:
  // Connects a |request| to a protocol capability provided by another component.
  template <typename Interface>
  static zx_status_t Connect(ServiceDirectoryPtr svc, fidl::InterfaceRequest<Interface> request) {
    if (auto status = svc->Connect(std::move(request)); status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to connect to " << Interface::Name_ << ": "
                     << zx_status_get_string(status);
      return status;
    }
    return ZX_OK;
  }

  LoopPtr loop_;
  ExecutorPtr executor_;
  ServiceDirectoryPtr svc_;
  OutgoingDirectoryPtr outgoing_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(ComponentContext);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_COMPONENT_CONTEXT_H_
