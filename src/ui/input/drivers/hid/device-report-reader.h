// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_HID_DEVICE_REPORT_READER_H_
#define SRC_UI_INPUT_DRIVERS_HID_DEVICE_REPORT_READER_H_

#include <fuchsia/hardware/input/llcpp/fidl.h>

#include <optional>

#include <fbl/mutex.h>
#include <fbl/ring_buffer.h>
#include <fbl/auto_lock.h>

namespace hid_driver {

class HidDevice;

class DeviceReportsReader
    : public ::llcpp::fuchsia::hardware::input::DeviceReportsReader::Interface {
 public:
  // The pointer to `base` must stay alive for as long as DeviceReportsReader
  // is alive.
  explicit DeviceReportsReader(HidDevice* base) : base_(base) {}

  ~DeviceReportsReader() {
    // The lock has to be grabbed to synchronize with any clients who are currently
    // trying access DeviceReportsReader.
    fbl::AutoLock lock(&reader_lock_);
    if (waiting_read_) {
      waiting_read_->ReplyError(ZX_ERR_PEER_CLOSED);
      waiting_read_.reset();
    }
  }

  void ReadReports(ReadReportsCompleter::Sync completer) override;
  zx_status_t WriteToFifo(const uint8_t* report, size_t report_len, zx_time_t time);

 private:
  zx_status_t SendReports() __TA_REQUIRES(reader_lock_);
  zx_status_t ReadReportFromFifo(uint8_t* buf, size_t buf_size, zx_time_t* time,
                                 size_t* out_report_size) __TA_REQUIRES(reader_lock_);
  fbl::Mutex reader_lock_;
  static constexpr size_t kDataFifoSize = 4096;
  fbl::RingBuffer<uint8_t, kDataFifoSize> data_fifo_ __TA_GUARDED(reader_lock_);
  fbl::RingBuffer<zx_time_t, llcpp::fuchsia::hardware::input::MAX_REPORTS_COUNT> timestamps_
      __TA_GUARDED(reader_lock_);

  std::optional<ReadReportsCompleter::Async> waiting_read_ __TA_GUARDED(reader_lock_);
  uint32_t trace_id_ = 0;
  uint32_t reports_written_ __TA_GUARDED(reader_lock_) = 0;
  // The number of reports sent out to the client.
  uint32_t reports_sent_ __TA_GUARDED(reader_lock_) = 0;
  HidDevice* const base_;
};

}  // namespace hid_driver

#endif  // SRC_UI_INPUT_DRIVERS_HID_DEVICE_REPORT_READER_H_
