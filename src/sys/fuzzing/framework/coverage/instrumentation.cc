// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/coverage/instrumentation.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace fuzzing {

using fuchsia::fuzzer::Payload;

InstrumentationImpl::InstrumentationImpl(uint64_t target_id, OptionsPtr options,
                                         AsyncDequePtr<CoverageEvent> events)
    : target_id_(target_id), options_(std::move(options)), events_(std::move(events)) {}

// FIDL methods.
void InstrumentationImpl::Initialize(InstrumentedProcess instrumented,
                                     InitializeCallback callback) {
  CoverageEvent event;
  event.target_id = target_id_;
  event.payload = Payload::WithProcessStarted(std::move(instrumented));
  if (auto status = events_->Send(std::move(event)); status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to initialize process: " << zx_status_get_string(status);
  }
  callback(CopyOptions(*options_));
}

void InstrumentationImpl::AddLlvmModule(LlvmModule llvm_module, AddLlvmModuleCallback callback) {
  CoverageEvent event;
  event.target_id = target_id_;
  event.payload = Payload::WithLlvmModuleAdded(std::move(llvm_module));
  if (auto status = events_->Send(std::move(event)); status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to add module: " << zx_status_get_string(status);
  }
  callback();
}

}  // namespace fuzzing
