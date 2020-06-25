// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "reports_reader.h"

#include <lib/async/cpp/task.h>

#include <fbl/auto_lock.h>

#include "fake.h"

namespace fake_input_report_device {

void FakeInputReportsReader::ReadInputReports(ReadInputReportsCallback callback) {
  fbl::AutoLock lock(&lock_);
  fuchsia::input::report::InputReportsReader_ReadInputReports_Result result;
  if (callback_) {
    callback(fuchsia::input::report::InputReportsReader_ReadInputReports_Result::WithErr(
        ZX_ERR_ALREADY_BOUND));
    return;
  }
  callback_ = std::move(callback);
  CallbackLocked();
}

void FakeInputReportsReader::QueueCallback() {
  fbl::AutoLock lock(&lock_);
  // We have to post this on the dispatcher because HLCPP has to be called on the same thread.
  async::PostTask(binding_.dispatcher(), [this]() { Callback(); });
}

void FakeInputReportsReader::Callback() {
  fbl::AutoLock lock(&lock_);
  CallbackLocked();
}
void FakeInputReportsReader::CallbackLocked() {
  if (!callback_) {
    return;
  }
  auto reports = device_->ReadReports();
  if (reports.size() == 0) {
    return;
  }
  fuchsia::input::report::InputReportsReader_ReadInputReports_Response response(std::move(reports));
  (*callback_)(fuchsia::input::report::InputReportsReader_ReadInputReports_Result::WithResponse(
      std::move(response)));
  callback_.reset();
}

}  // namespace fake_input_report_device
