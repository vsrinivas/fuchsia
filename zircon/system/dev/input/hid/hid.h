// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_INPUT_HID_HID_H_
#define ZIRCON_SYSTEM_DEV_INPUT_HID_HID_H_

#include <fuchsia/hardware/input/c/fidl.h>

#include <array>
#include <vector>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/hidbus.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>

#include "hid-fifo.h"
#include "hid-parser.h"

namespace hid_driver {

class HidDevice;

struct HidInstance : public fbl::DoublyLinkedListable<HidInstance*> {
  zx_device_t* zxdev;
  HidDevice* base;

  uint32_t flags;

  zx_hid_fifo_t fifo;
  uint32_t trace_id;
  uint32_t reports_written;
  uint32_t reports_read;
};

using DeviceType = ddk::Device<HidDevice, ddk::Unbindable, ddk::Openable>;

class HidDevice : public DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_HID_DEVICE> {
 public:
  explicit HidDevice(zx_device_t* parent) : DeviceType(parent) {}
  ~HidDevice() = default;

  zx_status_t Bind(ddk::HidbusProtocolClient hidbus_proto);
  void DdkRelease();
  zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
  void DdkUnbind();

  static void IoQueue(void* cookie, const void* _buf, size_t len);

  input_report_size_t GetMaxInputReportSize();
  input_report_size_t GetReportSizeById(input_report_id_t id,
                                        fuchsia_hardware_input_ReportType type);
  fuchsia_hardware_input_BootProtocol GetBootProtocol();

  ddk::HidbusProtocolClient* GetHidbusProtocol() { return &hidbus_; }

  void RemoveHidInstanceFromList(HidInstance* instance);

  size_t GetReportDescLen() { return hid_report_desc_.size(); }
  const uint8_t* GetReportDesc() { return hid_report_desc_.data(); }
  size_t GetNumReports() { return num_reports_; }

  // Needs to be called with an array of size |fuchsia_hardware_input_MAX_REPORT_IDS|.
  void GetReportIds(uint8_t* report_ids);

 private:
  // TODO(dgilhooley): Don't hardcode this limit
  static constexpr size_t kHidMaxReportIds = 32;

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

  size_t num_reports_ = 0;
  std::array<hid_report_size_t, kHidMaxReportIds> sizes_;

  fbl::Mutex instance_lock_;
  // Unmanaged linked-list because the HidInstances free themselves through DdkRelease.
  fbl::DoublyLinkedList<HidInstance*> instance_list_ __TA_GUARDED(instance_lock_);

  std::array<char, ZX_DEVICE_NAME_MAX + 1> name_;
};

}  // namespace hid_driver

#endif  // ZIRCON_SYSTEM_DEV_INPUT_HID_HID_H_
