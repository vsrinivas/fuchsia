// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_HID_INPUT_REPORT_INPUT_REPORTS_READER_H_
#define SRC_UI_INPUT_DRIVERS_HID_INPUT_REPORT_INPUT_REPORTS_READER_H_

#include <fuchsia/input/report/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/llcpp/buffer_then_heap_allocator.h>

#include <fbl/mutex.h>
#include <fbl/ring_buffer.h>

#include "src/ui/input/lib/hid-input-report/device.h"

namespace hid_input_report_dev {

namespace fuchsia_input_report = ::llcpp::fuchsia::input::report;

class InputReportsReader;

class InputReportBase {
 public:
  virtual void RemoveReaderFromList(InputReportsReader* reader) = 0;
};

class InputReportsReader : public ::llcpp::fuchsia::input::report::InputReportsReader::Interface {
 public:
  // The InputReportBase has to exist for the lifetime of the InputReportsReader.
  // The pointer to InputReportBase is unowned.
  // InputReportsReader will be freed by InputReportBase.
  static std::unique_ptr<InputReportsReader> Create(InputReportBase* base, uint32_t reader_id,
                                                    async_dispatcher_t* dispatcher,
                                                    zx::channel req);

  explicit InputReportsReader(InputReportBase* base, uint32_t reader_id)
      : reader_id_(reader_id), base_(base) {}

  void ReceiveReport(const uint8_t* report, size_t report_size, zx_time_t time,
                     hid_input_report::Device* device);

  // FIDL functions.
  void ReadInputReports(ReadInputReportsCompleter::Sync completer) override;

 private:
  // This is the static size that is used to allocate this instance's InputReports that
  // are stored in `reports_data`. This amount of memory is allocated with the driver
  // when the driver is initialized. If the `InputReports` go over this limit the
  // rest of the memory will be heap allocated as unique pointers.
  static constexpr size_t kFidlReportBufferSize = 8192;

  void SendReportsToWaitingRead() __TA_REQUIRES(readers_lock_);

  const uint32_t reader_id_;
  fbl::Mutex readers_lock_;
  std::optional<InputReportsReader::ReadInputReportsCompleter::Async> waiting_read_
      __TA_GUARDED(readers_lock_);
  std::optional<fidl::ServerBindingRef<llcpp::fuchsia::input::report::InputReportsReader>> binding_
      __TA_GUARDED(readers_lock_);
  fidl::BufferThenHeapAllocator<kFidlReportBufferSize> report_allocator_
      __TA_GUARDED(readers_lock_);
  fbl::RingBuffer<fuchsia_input_report::InputReport, fuchsia_input_report::MAX_DEVICE_REPORT_COUNT>
      reports_data_ __TA_GUARDED(readers_lock_);

  InputReportBase* base_;
};

}  // namespace hid_input_report_dev

#endif  // SRC_UI_INPUT_DRIVERS_HID_INPUT_REPORT_INPUT_REPORTS_READER_H_
