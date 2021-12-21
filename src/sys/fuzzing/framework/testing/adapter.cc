// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/testing/adapter.h"

#include <lib/syslog/cpp/macros.h>

#include <string>
#include <vector>

#include "src/sys/fuzzing/common/dispatcher.h"

namespace fuzzing {

FakeTargetAdapter::FakeTargetAdapter() : binding_(this) { wsync_.Signal(); }

fidl::InterfaceRequestHandler<TargetAdapter> FakeTargetAdapter::GetHandler() {
  return [&](fidl::InterfaceRequest<TargetAdapter> request) {
    coordinator_.Reset();
    binding_.Bind(std::move(request));
  };
}

void FakeTargetAdapter::SetParameters(const std::vector<std::string>& parameters) {
  parameters_ = std::vector<std::string>(parameters.begin(), parameters.end());
}

void FakeTargetAdapter::GetParameters(GetParametersCallback callback) {
  callback(std::vector<std::string>(parameters_.begin(), parameters_.end()));
}

void FakeTargetAdapter::Connect(zx::eventpair eventpair, Buffer test_input,
                                ConnectCallback callback) {
  test_input_.LinkReserved(std::move(test_input));
  coordinator_.Pair(std::move(eventpair), [this](zx_signals_t observed) {
    wsync_.WaitFor("signal to be received");
    wsync_.Reset();
    observed_ = observed;
    rsync_.Signal();
    return observed == kStart;
  });
  callback();
}

zx_signals_t FakeTargetAdapter::AwaitSignal() {
  rsync_.WaitFor("signal to be sent");
  rsync_.Reset();
  auto observed = observed_;
  wsync_.Signal();
  return observed;
}

zx_status_t FakeTargetAdapter::AwaitSignal(zx::time deadline, zx_signals_t* out) {
  auto status = rsync_.WaitUntil(deadline);
  if (status != ZX_OK) {
    return status;
  }
  rsync_.Reset();
  if (out) {
    *out = observed_;
  }
  wsync_.Signal();
  return ZX_OK;
}

void FakeTargetAdapter::SignalPeer(Signal signal) { coordinator_.SignalPeer(signal); }

}  // namespace fuzzing
