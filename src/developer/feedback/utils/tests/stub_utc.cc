// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/tests/stub_utc.h"

#include "lib/async/cpp/task.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace feedback {

using fuchsia::time::UtcSource;
using fuchsia::time::UtcState;

StubUtc::~StubUtc() {
  FXL_CHECK(Done()) << fxl::StringPrintf(
      "Expected %ld more calls to WatchState() (%ld/%lu calls made)",
      std::distance(next_reponse_, responses_.cend()),
      std::distance(responses_.cbegin(), next_reponse_), responses_.size());
}

void StubUtc::WatchState(WatchStateCallback callback) {
  FXL_CHECK(!Done()) << fxl::StringPrintf(
      "No more calls to WatchState() expected (%lu/%lu calls made)", responses_.size(),
      responses_.size());

  UtcState state;
  switch (next_reponse_->value) {
    case Response::Value::kNoResponse:
      ++next_reponse_;
      return;
    case Response::Value::kExternal:
      state.set_source(UtcSource::EXTERNAL);
      break;
    case Response::Value::kBackstop:
      state.set_source(UtcSource::BACKSTOP);
      break;
  }

  async::PostDelayedTask(
      dispatcher_,
      [callback = std::move(callback), state = std::move(state)]() mutable {
        callback(std::move(state));
      },
      next_reponse_->delay);

  ++next_reponse_;
}

bool StubUtc::Done() { return next_reponse_ == responses_.cend(); }

}  // namespace feedback
