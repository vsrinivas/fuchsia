// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_VMM_CONTROLLER_H_
#define SRC_VIRTUALIZATION_BIN_VMM_VMM_CONTROLLER_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include "src/virtualization/bin/vmm/vmm.h"

namespace vmm {

// The controller object for a VM. This is not thread safe, and should be run on the main thread
// with its dispatcher.
class VmmController : ::fuchsia::virtualization::GuestLifecycle {
 public:
  VmmController(fit::function<void()> stop_component_callback,
                std::unique_ptr<sys::ComponentContext> context, async_dispatcher_t* dispatcher);

  // |fuchsia::virtualization::GuestLifecycle|
  void Create(::fuchsia::virtualization::GuestConfig guest_config,
              CreateCallback callback) override;
  void Run(RunCallback callback) override;
  void Stop(StopCallback callback) override;

  void ProvideVmmForTesting(std::unique_ptr<vmm::Vmm> test_vmm) { vmm_ = std::move(test_vmm); }

 private:
  // Invoked if the lifecycle channel is closed. This ensures that this component will be cleaned
  // up if the component controlling this goes away.
  void LifecycleChannelClosed();

  // Schedules a task to destroy the VMM. This can safely be called from within the VMM via a
  // provided callback (typically by a VCPU upon guest exit). If a run callback is pending,
  // it will be completed with the provided status.
  //
  // If this can't schedule a task for whatever reason, this will shutdown the dispatch loop
  // ultimately resulting in this component going away.
  void ScheduleVmmTeardown(fit::result<::fuchsia::virtualization::GuestError> result);

  // Destroys the VMM and responds with the provided status is a responder is waiting.
  void DestroyAndRespond(fit::result<::fuchsia::virtualization::GuestError> result);

  std::unique_ptr<vmm::Vmm> vmm_;
  std::optional<RunCallback> run_callback_;
  fit::function<void()> stop_component_callback_;

  std::unique_ptr<sys::ComponentContext> context_;
  async_dispatcher_t* dispatcher_ = nullptr;  // Unowned.
  fidl::BindingSet<fuchsia::virtualization::GuestLifecycle> bindings_;
};

}  // namespace vmm

#endif  // SRC_VIRTUALIZATION_BIN_VMM_VMM_CONTROLLER_H_
