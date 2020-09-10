// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_HID_HID_H_
#define SRC_UI_INPUT_DRIVERS_HID_HID_H_

#include <array>
#include <memory>
#include <vector>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/hidbus.h>
#include <ddktl/protocol/hiddevice.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <hid-parser/item.h>
#include <hid-parser/parser.h>
#include <hid-parser/usages.h>

#include "hid-fifo.h"
#include "hid-instance.h"

namespace hid_driver {

typedef uint8_t input_report_id_t;

class HidDevice;

using HidDeviceType = ddk::Device<HidDevice, ddk::Unbindable, ddk::Openable>;

class HidDevice : public HidDeviceType,
                  public ddk::HidDeviceProtocol<HidDevice, ddk::base_protocol> {
 public:
  explicit HidDevice(zx_device_t* parent) : HidDeviceType(parent) {}
  ~HidDevice() = default;

  zx_status_t Bind(ddk::HidbusProtocolClient hidbus_proto);
  void DdkRelease();
  zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
  void DdkUnbind(ddk::UnbindTxn txn);

  // |HidDeviceProtocol|
  zx_status_t HidDeviceRegisterListener(const hid_report_listener_protocol_t* listener);
  // |HidDeviceProtocol|
  void HidDeviceUnregisterListener();
  // |HidDeviceProtocol|
  zx_status_t HidDeviceGetDescriptor(uint8_t* out_descriptor_data, size_t descriptor_count,
                                     size_t* out_descriptor_actual);
  // |HidDeviceProtocol|
  zx_status_t HidDeviceGetReport(hid_report_type_t rpt_type, uint8_t rpt_id,
                                 uint8_t* out_report_data, size_t report_count,
                                 size_t* out_report_actual);
  // |HidDeviceProtocol|
  zx_status_t HidDeviceSetReport(hid_report_type_t rpt_type, uint8_t rpt_id,
                                 const uint8_t* report_data, size_t report_count);
  // |HidDeviceProtocol|
  void HidDeviceGetHidDeviceInfo(hid_device_info_t* out_info);

  static void IoQueue(void* cookie, const void* _buf, size_t len, zx_time_t time);

  size_t GetMaxInputReportSize();
  size_t GetReportSizeById(input_report_id_t id, ReportType type);
  BootProtocol GetBootProtocol();
  hid_info_t GetHidInfo() { return info_; }

  ddk::HidbusProtocolClient* GetHidbusProtocol() { return &hidbus_; }

  void RemoveHidInstanceFromList(HidInstance* instance);

  size_t GetReportDescLen() { return hid_report_desc_.size(); }
  const uint8_t* GetReportDesc() { return hid_report_desc_.data(); }

  const char* GetName();

 private:
  zx_status_t ProcessReportDescriptor();
  zx_status_t InitReassemblyBuffer();
  void ReleaseReassemblyBuffer();
  zx_status_t SetReportDescriptor();

  hid_info_t info_ = {};
  ddk::HidbusProtocolClient hidbus_;

  // Reassembly buffer for input events too large to fit in a single interrupt
  // transaction.
  uint8_t* rbuf_ = nullptr;
  size_t rbuf_size_ = 0;
  size_t rbuf_filled_ = 0;
  size_t rbuf_needed_ = 0;

  std::vector<uint8_t> hid_report_desc_;

  hid::DeviceDescriptor* parsed_hid_desc_ = nullptr;
  size_t num_reports_ = 0;

  fbl::Mutex instance_lock_;
  // Unmanaged linked-list because the HidInstances free themselves through DdkRelease.
  fbl::DoublyLinkedList<HidInstance*> instance_list_ __TA_GUARDED(instance_lock_);

  std::array<char, ZX_DEVICE_NAME_MAX + 1> name_;

  fbl::Mutex listener_lock_;
  ddk::HidReportListenerProtocolClient report_listener_ __TA_GUARDED(listener_lock_);
};

}  // namespace hid_driver

#endif  // SRC_UI_INPUT_DRIVERS_HID_HID_H_
