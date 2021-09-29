// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/testing/transceiver.h"

#include <lib/sync/completion.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace fuzzing {

FidlInput FakeTransceiver::Transmit(const Input& input) {
  std::lock_guard<std::mutex> lock(mutex_);
  sync_completion_t sync;
  FidlInput fidl_input;
  transceiver_.Transmit(input, [&](FidlInput response) {
    fidl_input = std::move(response);
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  return fidl_input;
}

Input FakeTransceiver::Receive(FidlInput fidl_input) {
  std::lock_guard<std::mutex> lock(mutex_);
  sync_completion_t sync;
  Input received;
  transceiver_.Receive(std::move(fidl_input), [&sync, &received](zx_status_t status, Input input) {
    FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
    received = std::move(input);
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  return received;
}

}  // namespace fuzzing
