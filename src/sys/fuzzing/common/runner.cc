// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/runner.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

#include "src/sys/fuzzing/common/status.h"

namespace fuzzing {

Runner::Runner(ExecutorPtr executor) : executor_(executor), monitors_(executor) {}

///////////////////////////////////////////////////////////////
// Workflow methods

ZxPromise<> Runner::Workflow::Start() {
  return fpromise::make_promise([this]() -> ZxResult<> {
    if (completer_) {
      FX_LOGS(WARNING) << "Another fuzzing workflow is already in progress.";
      return fpromise::error(ZX_ERR_BAD_STATE);
    }
    ZxBridge<> bridge;
    completer_ = std::move(bridge.completer);
    consumer_ = std::move(bridge.consumer);
    runner_->StartWorkflow(scope_);
    return fpromise::ok();
  });
}

ZxPromise<FuzzResult> Runner::Execute(Input input) {
  std::vector<Input> inputs;
  inputs.emplace_back(std::move(input));
  return Execute(std::move(inputs));
}

ZxPromise<> Runner::Workflow::Stop() {
  return consumer_ ? consumer_.promise_or(fpromise::error(ZX_ERR_CANCELED)).box()
                   : fpromise::make_promise([]() -> ZxResult<> { return fpromise::ok(); });
}

void Runner::Workflow::Finish() {
  if (completer_) {
    runner_->FinishWorkflow();
    completer_.complete_ok();
  }
}

///////////////////////////////////////////////////////////////
// Status-related methods.

void Runner::AddMonitor(fidl::InterfaceHandle<Monitor> monitor) {
  monitors_.Add(std::move(monitor));
}

void Runner::UpdateMonitors(UpdateReason reason) {
  monitors_.set_status(CollectStatus());
  monitors_.Update(reason);
}

}  // namespace fuzzing
