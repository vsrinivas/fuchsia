// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/coverage/instrumentation.h"

namespace fuzzing {

InstrumentationImpl::InstrumentationImpl(uint64_t target_id,
                                         std::shared_ptr<CoverageEventQueue> events)
    : target_id_(target_id), events_(events) {}

// FIDL methods.
void InstrumentationImpl::Initialize(InstrumentedProcess instrumented,
                                     InitializeCallback callback) {
  events_->AddProcess(target_id_, std::move(instrumented));
  callback(events_->GetOptions());
}

void InstrumentationImpl::AddLlvmModule(LlvmModule llvm_module, AddLlvmModuleCallback callback) {
  events_->AddLlvmModule(target_id_, std::move(llvm_module));
  callback();
}

}  // namespace fuzzing
