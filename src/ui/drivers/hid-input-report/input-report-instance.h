// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_DRIVERS_HID_INPUT_REPORT_INPUT_REPORT_INSTANCE_H_
#define SRC_UI_DRIVERS_HID_INPUT_REPORT_INPUT_REPORT_INSTANCE_H_

#include <array>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/ring_buffer.h>
#include <hid-input-report/descriptors.h>
#include <hid-input-report/device.h>
#include <hid-input-report/fidl.h>
#include <hid-input-report/mouse.h>

namespace hid_input_report_dev {

namespace llcpp_report = ::llcpp::fuchsia::input::report;

class InputReportBase;

class InputReportInstance;
using InstanceDeviceType = ddk::Device<InputReportInstance, ddk::Closable, ddk::Messageable>;

class InputReportInstance : public InstanceDeviceType,
                            ::llcpp::fuchsia::input::report::InputDevice::Interface,
                            public fbl::DoublyLinkedListable<InputReportInstance*> {
 public:
  InputReportInstance(zx_device_t* parent) : InstanceDeviceType(parent) {}

  // The |InputReportBase| is responsible for creating |InputReportInstance| and adding it to
  // the LinkedList of instances that are owned by the base. The Instance is a child driver
  // of the base and can not outlive the base. The Instance driver must remove itself from
  // the LinkedList of it's Base driver during DdkClose.
  zx_status_t Bind(InputReportBase* base);

  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  void DdkRelease() { delete this; }
  zx_status_t DdkClose(uint32_t flags);

  void ReceiveReport(const hid_input_report::ReportDescriptor& descriptor,
                     const hid_input_report::Report& input_report);

  // FIDL functions.
  void GetReportsEvent(GetReportsEventCompleter::Sync _completer);
  void GetReports(GetReportsCompleter::Sync _completer);
  void GetDescriptor(GetDescriptorCompleter::Sync _completer);

 private:
  fbl::Mutex report_lock_;
  zx::event reports_event_ __TA_GUARDED(report_lock_);
  // The ring buffer stores the hid reports as they are sent to the instance.
  fbl::RingBuffer<hid_input_report::Report, llcpp_report::MAX_DEVICE_REPORT_COUNT> reports_data_
      __TA_GUARDED(report_lock_);
  // These two arrays store the information to build the FIDL tables.
  std::array<hid_input_report::FidlReport, llcpp_report::MAX_DEVICE_REPORT_COUNT> reports_fidl_data_
      __TA_GUARDED(report_lock_);
  std::array<llcpp_report::InputReport, llcpp_report::MAX_DEVICE_REPORT_COUNT> reports_
      __TA_GUARDED(report_lock_);

  InputReportBase* base_ = nullptr;
};

}  // namespace hid_input_report_dev

#endif  // SRC_UI_DRIVERS_HID_INPUT_REPORT_INPUT_REPORT_INSTANCE_H_
