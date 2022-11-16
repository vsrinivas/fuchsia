// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_HID_INPUT_REPORT_INPUT_REPORT_H_
#define SRC_UI_INPUT_DRIVERS_HID_INPUT_REPORT_INPUT_REPORT_H_

#include <fuchsia/hardware/hiddevice/cpp/banjo.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/result.h>
#include <lib/zx/time.h>

#include <list>
#include <optional>
#include <vector>

#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>

#include "input-reports-reader.h"
#include "src/ui/input/lib/hid-input-report/device.h"

namespace hid_input_report_dev {

class InputReport : public fidl::WireServer<fuchsia_input_report::InputDevice>,
                    public InputReportBase,
                    ddk::HidReportListenerProtocol<InputReport> {
 public:
  explicit InputReport(ddk::HidDeviceProtocolClient hiddev) : hiddev_(hiddev) {}
  virtual ~InputReport() = default;

  zx_status_t Start();
  zx_status_t Stop();

  // HidReportListener functions.
  void HidReportListenerReceiveReport(const uint8_t* report, size_t report_size,
                                      zx_time_t report_time);

  // InputReportBase functions.
  void RemoveReaderFromList(InputReportsReader* reader) override;

  // FIDL functions.
  void GetInputReportsReader(GetInputReportsReaderRequestView request,
                             GetInputReportsReaderCompleter::Sync& completer) override;
  void GetDescriptor(GetDescriptorCompleter::Sync& completer) override;
  void SendOutputReport(SendOutputReportRequestView request,
                        SendOutputReportCompleter::Sync& completer) override;
  void GetFeatureReport(GetFeatureReportCompleter::Sync& completer) override;
  void SetFeatureReport(SetFeatureReportRequestView request,
                        SetFeatureReportCompleter::Sync& completer) override;
  void GetInputReport(GetInputReportRequestView request,
                      GetInputReportCompleter::Sync& completer) override;

  // Function for testing that blocks until a new reader is connected.
  zx_status_t WaitForNextReader(zx::duration timeout);

  zx::vmo InspectVmo() { return inspector_.DuplicateVmo(); }
  inspect::Inspector& Inspector() { return inspector_; }

 private:
  // This is the static size that is used to allocate this instance's InputDescriptor.
  // This amount of memory is stack allocated when a client calls GetDescriptor.
  static constexpr size_t kFidlDescriptorBufferSize = 8192;

  static constexpr uint64_t kLatencyBucketCount = 7;
  static constexpr zx::duration kLatencyFloor = zx::msec(5);
  static constexpr zx::duration kLatencyInitialStep = kLatencyFloor;
  static constexpr uint64_t kLatencyStepMultiplier = 3;

  static zx::result<hid_input_report::DeviceType> InputReportDeviceTypeToHid(
      fuchsia_input_report::wire::DeviceType type);

  bool ParseHidInputReportDescriptor(const hid::ReportDescriptor* report_desc);

  // If we have a consumer control device, get a report and send it to the reader,
  // since the reader needs the device's state.
  void SendInitialConsumerControlReport(InputReportsReader* reader);

  std::string GetDeviceTypesString() const;

  ddk::HidDeviceProtocolClient hiddev_;

  std::vector<std::unique_ptr<hid_input_report::Device>> devices_;

  fbl::Mutex readers_lock_;
  uint32_t next_reader_id_ __TA_GUARDED(readers_lock_) = 0;
  std::list<std::unique_ptr<InputReportsReader>> readers_list_ __TA_GUARDED(readers_lock_);
  std::optional<async::Loop> loop_ __TA_GUARDED(readers_lock_);
  sync_completion_t next_reader_wait_;

  inspect::Inspector inspector_;
  inspect::Node root_;
  inspect::ExponentialUintHistogram latency_histogram_usecs_;
  inspect::UintProperty average_latency_usecs_;
  inspect::UintProperty max_latency_usecs_;
  inspect::StringProperty device_types_;
  inspect::UintProperty total_report_count_;
  inspect::UintProperty last_event_timestamp_;

  uint64_t report_count_ = 0;
  zx::duration total_latency_ = {};
  zx::duration max_latency_ = {};

  uint32_t sensor_count_ = 0;
};

}  // namespace hid_input_report_dev

#endif  // SRC_UI_INPUT_DRIVERS_HID_INPUT_REPORT_INPUT_REPORT_H_
