// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/adapters/llvm.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/status.h>

namespace fuzzing {

LLVMTargetAdapter::LLVMTargetAdapter() : binding_(this) {}

fidl::InterfaceRequestHandler<TargetAdapter> LLVMTargetAdapter::GetHandler(
    fit::closure on_close, async_dispatcher_t* dispatcher) {
  on_close_ = std::move(on_close);
  binding_.set_dispatcher(dispatcher);
  return
      [this](fidl::InterfaceRequest<TargetAdapter> request) { binding_.Bind(std::move(request)); };
}

void LLVMTargetAdapter::Connect(zx::eventpair eventpair, Buffer test_input,
                                ConnectCallback callback) {
  test_input_.LinkReserved(std::move(test_input));
  test_input_.SetPoisoning(true);
  coordinator_.Pair(std::move(eventpair),
                    [this](zx_signals_t observed) { return OnSignal(observed); });
  callback();
}

bool LLVMTargetAdapter::OnSignal(zx_signals_t observed) {
  if (observed & ZX_EVENTPAIR_PEER_CLOSED) {
    on_close_();
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

}  // namespace fuzzing
