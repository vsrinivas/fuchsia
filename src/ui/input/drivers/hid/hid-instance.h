// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_HID_HID_INSTANCE_H_
#define SRC_UI_INPUT_DRIVERS_HID_HID_INSTANCE_H_

#include <fidl/fuchsia.hardware.input/cpp/wire.h>
#include <fuchsia/hardware/hidbus/cpp/banjo.h>
#include <fuchsia/hardware/hiddevice/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/fidl-async/cpp/bind.h>

#include <array>
#include <list>
#include <memory>
#include <vector>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/ring_buffer.h>

#include "device-report-reader.h"
#include "hid-fifo.h"

namespace hid_driver {

class HidDevice;

using fuchsia_hardware_input::wire::BootProtocol;
using fuchsia_hardware_input::wire::ReportType;

class HidInstance;
using HidInstanceDeviceType = ddk::Device<HidInstance, ddk::Closable,
                                          ddk::Messageable<fuchsia_hardware_input::Device>::Mixin>;

class HidInstance : public HidInstanceDeviceType,
                    public fbl::DoublyLinkedListable<HidInstance*>,
                    public ddk::EmptyProtocol<ZX_PROTOCOL_HID_DEVICE> {
 public:
  explicit HidInstance(zx_device_t* parent)
      : HidInstanceDeviceType(parent), loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    zx_hid_fifo_init(&fifo_);
  }
  ~HidInstance() = default;

  zx_status_t Bind(HidDevice* base);
  void DdkRelease();
  zx_status_t DdkClose(uint32_t flags);

  void GetBootProtocol(GetBootProtocolRequestView request,
                       GetBootProtocolCompleter::Sync& _completer) override;
  void GetDeviceIds(GetDeviceIdsRequestView request,
                    GetDeviceIdsCompleter::Sync& _completer) override;
  void GetReportDesc(GetReportDescRequestView request,
                     GetReportDescCompleter::Sync& _completer) override;
  void GetReportsEvent(GetReportsEventRequestView request,
                       GetReportsEventCompleter::Sync& _completer) override;
  void GetReport(GetReportRequestView request, GetReportCompleter::Sync& _completer) override;
  void SetReport(SetReportRequestView request, SetReportCompleter::Sync& _completer) override;
  void SetTraceId(SetTraceIdRequestView request, SetTraceIdCompleter::Sync& _completer) override;
  void ReadReports(ReadReportsRequestView request, ReadReportsCompleter::Sync& _completer) override;
  void ReadReport(ReadReportRequestView request, ReadReportCompleter::Sync& completer) override;
  void GetDeviceReportsReader(GetDeviceReportsReaderRequestView request,
                              GetDeviceReportsReaderCompleter::Sync& completer) override;

  void CloseInstance();
  void WriteToFifo(const uint8_t* report, size_t report_len, zx_time_t time);

 private:
  void SetReadable();
  void ClearReadable();
  zx_status_t ReadReportFromFifo(uint8_t* buf, size_t buf_size, zx_time_t* time,
                                 size_t* report_size) __TA_REQUIRES(fifo_lock_);
  HidDevice* base_ = nullptr;

  uint32_t flags_ = 0;

  fbl::Mutex fifo_lock_;
  zx_hid_fifo_t fifo_ __TA_GUARDED(fifo_lock_) = {};
  static const size_t kMaxNumReports = 50;
  fbl::RingBuffer<zx_time_t, kMaxNumReports> timestamps_ __TA_GUARDED(fifo_lock_);

  zx::event fifo_event_;

  uint32_t trace_id_ = 0;
  uint32_t reports_written_ = 0;
  // The number of reports sent out to the client.
  uint32_t reports_sent_ = 0;

  fbl::Mutex readers_lock_;
  bool loop_started_ __TA_GUARDED(readers_lock_) = false;
  async::Loop loop_ __TA_GUARDED(readers_lock_);
  std::list<std::unique_ptr<DeviceReportsReader>> readers_ __TA_GUARDED(readers_lock_);
};

}  // namespace hid_driver

#endif  // SRC_UI_INPUT_DRIVERS_HID_HID_INSTANCE_H_
