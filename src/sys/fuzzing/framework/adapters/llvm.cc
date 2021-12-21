// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/adapters/llvm.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <string>
#include <vector>

namespace fuzzing {

LLVMTargetAdapter::LLVMTargetAdapter() : binding_(this) {}

LLVMTargetAdapter::~LLVMTargetAdapter() { connected_.Signal(); }

fidl::InterfaceRequestHandler<TargetAdapter> LLVMTargetAdapter::GetHandler() {
  return
      [this](fidl::InterfaceRequest<TargetAdapter> request) { binding_.Bind(std::move(request)); };
}

void LLVMTargetAdapter::SetParameters(const std::vector<std::string>& parameters) {
  parameters_ = std::vector<std::string>(parameters.begin(), parameters.end());
}

void LLVMTargetAdapter::GetParameters(GetParametersCallback callback) {
  callback(std::vector<std::string>(parameters_.begin(), parameters_.end()));
}

void LLVMTargetAdapter::Connect(zx::eventpair eventpair, Buffer test_input,
                                ConnectCallback callback) {
  test_input_.LinkReserved(std::move(test_input));
  test_input_.SetPoisoning(true);
  coordinator_.Pair(std::move(eventpair),
                    [this](zx_signals_t observed) { return OnSignal(observed); });
  callback();
  connected_.Signal();
}

bool LLVMTargetAdapter::OnSignal(zx_signals_t observed) {
  if (observed & ZX_EVENTPAIR_PEER_CLOSED) {
    return false;
  }
  if (observed != kStart) {
    FX_LOGS(ERROR) << "Unexpected signal: " << observed;
    return false;
  }
  auto result = LLVMFuzzerTestOneInput(test_input_.data(), test_input_.size());
  if (result) {
    FX_LOGS(FATAL) << "Fuzz target function returned non-zero result: " << result;
  }
  return coordinator_.SignalPeer(kFinish);
}

zx_status_t LLVMTargetAdapter::Run() {
  connected_.WaitFor("engine to connect");
  return binding_.AwaitClose();
}

}  // namespace fuzzing
