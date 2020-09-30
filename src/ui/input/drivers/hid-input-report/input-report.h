// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_HID_INPUT_REPORT_INPUT_REPORT_H_
#define SRC_UI_INPUT_DRIVERS_HID_INPUT_REPORT_INPUT_REPORT_H_

#include <list>
#include <vector>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/hiddevice.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>

#include "input-reports-reader.h"
#include "src/ui/input/lib/hid-input-report/device.h"

namespace hid_input_report_dev {

class InputReport;
using DeviceType = ddk::Device<InputReport, ddk::Unbindable, ddk::Messageable>;
class InputReport : public DeviceType,
                    public InputReportBase,
                    fuchsia_input_report::InputDevice::Interface,
                    ddk::HidReportListenerProtocol<InputReport>,
                    public ddk::EmptyProtocol<ZX_PROTOCOL_INPUTREPORT> {
 public:
  InputReport(zx_device_t* parent, ddk::HidDeviceProtocolClient hiddev)
      : DeviceType(parent), hiddev_(hiddev) {}
  virtual ~InputReport() = default;

  // DDK Functions.
  zx_status_t Bind();
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease() { delete this; }
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

  // HidReportListener functions.
  void HidReportListenerReceiveReport(const uint8_t* report, size_t report_size,
                                      zx_time_t report_time);

  // InputReportBase functions.
  void RemoveReaderFromList(InputReportsReader* reader) override;

  // FIDL functions.
  void GetInputReportsReader(zx::channel req,
                             GetInputReportsReaderCompleter::Sync completer) override;
  void GetDescriptor(GetDescriptorCompleter::Sync completer) override;
  void SendOutputReport(::llcpp::fuchsia::input::report::OutputReport report,
                        SendOutputReportCompleter::Sync completer) override;
  void GetFeatureReport(GetFeatureReportCompleter::Sync completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }
  void SetFeatureReport(::llcpp::fuchsia::input::report::FeatureReport report,
                        SetFeatureReportCompleter::Sync completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  // Function for testing that blocks until a new reader is connected.
  zx_status_t WaitForNextReader(zx::duration timeout);

 private:
  // This is the static size that is used to allocate this instance's InputDescriptor.
  // This amount of memory is stack allocated when a client calls GetDescriptor.
  static constexpr size_t kFidlDescriptorBufferSize = 8192;

  bool ParseHidInputReportDescriptor(const hid::ReportDescriptor* report_desc);

  // If we have a consumer control device, get a report and send it to the reader,
  // since the reader needs the device's state.
  void SendInitialConsumerControlReport(InputReportsReader* reader);

  ddk::HidDeviceProtocolClient hiddev_;

  std::vector<std::unique_ptr<hid_input_report::Device>> devices_;

  fbl::Mutex readers_lock_;
  uint32_t next_reader_id_ __TA_GUARDED(readers_lock_) = 0;
  std::list<std::unique_ptr<InputReportsReader>> readers_list_ __TA_GUARDED(readers_lock_);
  std::optional<async::Loop> loop_ __TA_GUARDED(readers_lock_);
  sync_completion_t next_reader_wait_;
};

}  // namespace hid_input_report_dev

#endif  // SRC_UI_INPUT_DRIVERS_HID_INPUT_REPORT_INPUT_REPORT_H_
