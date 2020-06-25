// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_TESTING_FAKE_INPUT_REPORT_DEVICE_REPORTS_READER_H_
#define SRC_UI_INPUT_TESTING_FAKE_INPUT_REPORT_DEVICE_REPORTS_READER_H_

#include <fuchsia/input/report/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/time.h>
#include <lib/fidl/cpp/binding_set.h>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

namespace fake_input_report_device {

class FakeInputDevice;

// Creates a fake class that vends the InputReportsReader API. This should be
// created and managed by FakeInputDevice.
// If this class is bound on a seperate thread, that thread must be joined before
// this class is destructed.
class FakeInputReportsReader final : public fuchsia::input::report::InputReportsReader {
 public:
  // Create a FakeInputReportsReader. The pointer to FakeInputDevice is unmanaged and
  // the FakeInputDevice must outlive FakeInputReportsReader.
  explicit FakeInputReportsReader(
      fidl::InterfaceRequest<fuchsia::input::report::InputReportsReader> request,
      async_dispatcher_t* dispatcher, FakeInputDevice* device)
      : binding_(this, std::move(request), dispatcher), device_(device) {
    // Create a wait that will be called on dispatcher's shutdown that will free our
    // callback if one exists. This is why the dispatcher must be shutdown before FakeInputDevice
    // is destructed.
    zx::event::create(0, &shutdown_event_);
    dispatcher_shutdown_.emplace(shutdown_event_.get());
    dispatcher_shutdown_->Begin(binding_.dispatcher(),
                                [this](async_dispatcher_t* dispatcher, async::WaitOnce* wait,
                                       zx_status_t status, const zx_packet_signal_t* signal) {
                                  fbl::AutoLock lock(&lock_);
                                  callback_.reset();
                                });
  }

  void ReadInputReports(ReadInputReportsCallback callback) override;

  // Queues up the ReadInputReports callback if one exists. The callback will be run on the
  // async_dispatcher.
  void QueueCallback();

 private:
  // Send the ReadInputReports callback. This can only be called on the async_dispatcher thread.
  void Callback();
  void CallbackLocked() __TA_REQUIRES(lock_);

  // `shutdown_event` should be first in order of declaration because it needs to be destructed
  // before `dispatcher_shutdown_`.
  zx::event shutdown_event_;
  std::optional<async::WaitOnce> dispatcher_shutdown_;

  fbl::Mutex lock_;
  fidl::Binding<fuchsia::input::report::InputReportsReader> binding_ __TA_GUARDED(lock_);
  std::optional<ReadInputReportsCallback> callback_ __TA_GUARDED(lock_);
  FakeInputDevice* device_;
};

}  // namespace fake_input_report_device

#endif  // SRC_UI_INPUT_TESTING_FAKE_INPUT_REPORT_DEVICE_REPORTS_READER_H_
