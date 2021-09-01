// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_HID_INPUT_REPORT_INPUT_REPORTS_READER_H_
#define SRC_UI_INPUT_DRIVERS_HID_INPUT_REPORT_INPUT_REPORTS_READER_H_

#include <fidl/fuchsia.input.report/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <fbl/mutex.h>
#include <fbl/ring_buffer.h>

#include "src/ui/input/lib/hid-input-report/device.h"

namespace hid_input_report_dev {

namespace fuchsia_input_report = fuchsia_input_report;

class InputReportsReader;

class InputReportBase {
 public:
  virtual void RemoveReaderFromList(InputReportsReader* reader) = 0;
};

class InputReportsReader : public fidl::WireServer<fuchsia_input_report::InputReportsReader> {
 public:
  // The InputReportBase has to exist for the lifetime of the InputReportsReader.
  // The pointer to InputReportBase is unowned.
  // InputReportsReader will be freed by InputReportBase.
  static std::unique_ptr<InputReportsReader> Create(
      InputReportBase* base, uint32_t reader_id, async_dispatcher_t* dispatcher,
      fidl::ServerEnd<fuchsia_input_report::InputReportsReader> request);

  explicit InputReportsReader(InputReportBase* base, uint32_t reader_id)
      : reader_id_(reader_id), base_(base) {}

  void ReceiveReport(const uint8_t* report, size_t report_size, zx_time_t time,
                     hid_input_report::Device* device);

  // FIDL functions.
  void ReadInputReports(ReadInputReportsRequestView request,
                        ReadInputReportsCompleter::Sync& completer) override;

 private:
  // This is the static size that is used to allocate this instance's InputReports that
  // are stored in `reports_data`. This amount of memory is allocated with the driver
  // when the driver is initialized. If the `InputReports` go over this limit the
  // rest of the memory will be heap allocated.
  static constexpr size_t kFidlReportBufferSize = 8192;

  void SendReportsToWaitingRead() __TA_REQUIRES(readers_lock_);

  const uint32_t reader_id_;
  fbl::Mutex readers_lock_;
  std::optional<InputReportsReader::ReadInputReportsCompleter::Async> waiting_read_
      __TA_GUARDED(readers_lock_);
  std::optional<fidl::ServerBindingRef<fuchsia_input_report::InputReportsReader>> binding_
      __TA_GUARDED(readers_lock_);
  fidl::Arena<kFidlReportBufferSize> report_allocator_ __TA_GUARDED(readers_lock_);
  fbl::RingBuffer<fuchsia_input_report::wire::InputReport,
                  fuchsia_input_report::wire::kMaxDeviceReportCount>
      reports_data_ __TA_GUARDED(readers_lock_);

  InputReportBase* base_;
};

}  // namespace hid_input_report_dev

#endif  // SRC_UI_INPUT_DRIVERS_HID_INPUT_REPORT_INPUT_REPORTS_READER_H_
