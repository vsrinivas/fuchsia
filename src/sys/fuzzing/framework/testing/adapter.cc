// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/testing/adapter.h"

#include <lib/syslog/cpp/macros.h>

#include <string>
#include <vector>

#include "src/sys/fuzzing/common/dispatcher.h"

namespace fuzzing {

FakeTargetAdapter::FakeTargetAdapter() : binding_(this) { sync_completion_signal(&wsync_); }

fidl::InterfaceRequestHandler<TargetAdapter> FakeTargetAdapter::GetHandler() {
  return [&](fidl::InterfaceRequest<TargetAdapter> request) {
    coordinator_.Reset();
    binding_.Bind(std::move(request));
  };
}

void FakeTargetAdapter::GetParameters(GetParametersCallback callback) {
  // Stub; actual implementation is in a subsequent CL.
  callback(std::vector<std::string>());
}

void FakeTargetAdapter::Connect(zx::eventpair eventpair, Buffer test_input,
                                ConnectCallback callback) {
  test_input_.LinkReserved(std::move(test_input));
  coordinator_.Pair(std::move(eventpair), [this](zx_signals_t observed) {
    sync_completion_wait(&wsync_, ZX_TIME_INFINITE);
    sync_completion_reset(&wsync_);
    observed_ = observed;
    sync_completion_signal(&rsync_);
    return !(observed & ZX_EVENTPAIR_PEER_CLOSED);
  });
  callback();
}

zx_signals_t FakeTargetAdapter::AwaitSignal() {
  zx_signals_t observed;
  AwaitSignal(zx::duration::infinite(), &observed);
  return observed;
}

zx_status_t FakeTargetAdapter::AwaitSignal(const zx::duration& timeout, zx_signals_t* out) {
  auto status = sync_completion_wait(&rsync_, timeout.get());
  if (status != ZX_OK) {
    return status;
  }
  sync_completion_reset(&rsync_);
  if (out) {
    *out = observed_;
  }
  sync_completion_signal(&wsync_);
  return ZX_OK;
}

void FakeTargetAdapter::SignalPeer(Signal signal) { coordinator_.SignalPeer(signal); }

}  // namespace fuzzing
