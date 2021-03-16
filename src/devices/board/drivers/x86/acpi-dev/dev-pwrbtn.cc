// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/hidbus/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <memory>
#include <utility>

#include <acpica/acpi.h>
#include <ddktl/device.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <hid/descriptor.h>

#include "acpi-private.h"
#include "dev.h"
#include "errors.h"

class AcpiPwrbtnDevice;
using DeviceType = ddk::Device<AcpiPwrbtnDevice>;

class AcpiPwrbtnDevice : public DeviceType,
                         public ddk::HidbusProtocol<AcpiPwrbtnDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(zx_device_t* parent, std::unique_ptr<AcpiPwrbtnDevice>* out);

  // hidbus protocol implementation
  zx_status_t HidbusQuery(uint32_t options, hid_info_t* info);
  zx_status_t HidbusStart(const hidbus_ifc_protocol_t* ifc);
  void HidbusStop();
  zx_status_t HidbusGetDescriptor(hid_description_type_t desc_type, uint8_t* out_data_buffer,
                                  size_t data_size, size_t* out_data_actual);
  zx_status_t HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, uint8_t* data, size_t len,
                              size_t* out_len);
  zx_status_t HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const uint8_t* data, size_t len);
  zx_status_t HidbusGetIdle(uint8_t rpt_id, uint8_t* duration);
  zx_status_t HidbusSetIdle(uint8_t rpt_id, uint8_t duration);
  zx_status_t HidbusGetProtocol(uint8_t* protocol);
  zx_status_t HidbusSetProtocol(uint8_t protocol);

  void DdkRelease();
  ~AcpiPwrbtnDevice();

 private:
  explicit AcpiPwrbtnDevice(zx_device_t* parent);
  DISALLOW_COPY_ASSIGN_AND_MOVE(AcpiPwrbtnDevice);

  static uint32_t FixedEventHandler(void* ctx);
  static void NotifyHandler(ACPI_HANDLE handle, UINT32 value, void* ctx);

  void HandlePress();
  void QueueHidReportLocked() TA_REQ(lock_);

  fbl::Mutex lock_;

  // Interface the driver is currently bound to
  ddk::HidbusIfcProtocolClient client_;

  // Track the pressed state.  We don't receive up-events from ACPI, but we
  // may want to synthesize them in the future if we care about duration of
  // press.
  bool pressed_ TA_GUARDED(lock_) = false;

  static const uint8_t kHidDescriptor[];
  static const size_t kHidDescriptorLen;
  static constexpr size_t kHidReportLen = 1;
};

// We encode the power button as a System Power Down control in a System Control
// collection.
const uint8_t AcpiPwrbtnDevice::kHidDescriptor[] = {
    HID_USAGE_PAGE(0x01),  // Usage Page (Generic Desktop)
    HID_USAGE(0x80),       // Usage (System Control)

    HID_COLLECTION_APPLICATION,
    HID_USAGE(0x81),  // Usage (System Power Down)
    HID_LOGICAL_MIN(0),
    HID_LOGICAL_MAX(1),
    HID_REPORT_COUNT(1),
    HID_REPORT_SIZE(1),  // 1 bit for power-down
    HID_INPUT(0x06),     // Input (Data,Var,Rel)
    HID_REPORT_SIZE(7),  // 7 bits of padding
    HID_INPUT(0x03),     // Input (Const,Var,Abs)
};

const size_t AcpiPwrbtnDevice::kHidDescriptorLen = sizeof(AcpiPwrbtnDevice::kHidDescriptor);

AcpiPwrbtnDevice::AcpiPwrbtnDevice(zx_device_t* parent) : DeviceType(parent) {}

AcpiPwrbtnDevice::~AcpiPwrbtnDevice() {
  AcpiRemoveNotifyHandler(ACPI_ROOT_OBJECT, ACPI_SYSTEM_NOTIFY | ACPI_DEVICE_NOTIFY, NotifyHandler);
  AcpiRemoveFixedEventHandler(ACPI_EVENT_POWER_BUTTON, FixedEventHandler);
}

void AcpiPwrbtnDevice::HandlePress() {
  zxlogf(DEBUG, "acpi-pwrbtn: pressed");

  fbl::AutoLock guard(&lock_);
  pressed_ = true;
  QueueHidReportLocked();
}

uint32_t AcpiPwrbtnDevice::FixedEventHandler(void* ctx) {
  auto dev = reinterpret_cast<AcpiPwrbtnDevice*>(ctx);

  dev->HandlePress();

  // Note that the spec indicates to return 0. The code in the
  // Intel implementation (AcpiEvFixedEventDetect) reads differently.
  return ACPI_INTERRUPT_HANDLED;
}

void AcpiPwrbtnDevice::NotifyHandler(ACPI_HANDLE handle, UINT32 value, void* ctx) {
  auto dev = reinterpret_cast<AcpiPwrbtnDevice*>(ctx);
  acpi::UniquePtr<ACPI_DEVICE_INFO> info;

  if (auto res = acpi::GetObjectInfo(handle); res.is_error()) {
    return;
  } else {
    info = std::move(res.value());
  }

  // Handle powerbutton events via the notify interface
  bool power_btn = false;
  if (info->Valid & ACPI_VALID_HID) {
    if (value == 128 && !strncmp(info->HardwareId.String, "PNP0C0C", info->HardwareId.Length)) {
      power_btn = true;
    } else if (value == 199 &&
               (!strncmp(info->HardwareId.String, "MSHW0028", info->HardwareId.Length) ||
                !strncmp(info->HardwareId.String, "MSHW0040", info->HardwareId.Length))) {
      power_btn = true;
    }
  }

  if (power_btn) {
    dev->HandlePress();
  }
}

void AcpiPwrbtnDevice::QueueHidReportLocked() {
  if (client_.is_valid()) {
    uint8_t report = 1;
    client_.IoQueue(&report, sizeof(report), zx_clock_get_monotonic());
  }
}

zx_status_t AcpiPwrbtnDevice::HidbusQuery(uint32_t options, hid_info_t* info) {
  zxlogf(DEBUG, "acpi-pwrbtn: hid bus query");

  info->dev_num = 0;
  info->device_class = HID_DEVICE_CLASS_OTHER;
  info->boot_device = false;
  return ZX_OK;
}

zx_status_t AcpiPwrbtnDevice::HidbusStart(const hidbus_ifc_protocol_t* ifc) {
  zxlogf(DEBUG, "acpi-pwrbtn: hid bus start");

  fbl::AutoLock guard(&lock_);
  if (client_.is_valid()) {
    return ZX_ERR_ALREADY_BOUND;
  }
  client_ = ddk::HidbusIfcProtocolClient(ifc);
  return ZX_OK;
}

void AcpiPwrbtnDevice::HidbusStop() {
  zxlogf(DEBUG, "acpi-pwrbtn: hid bus stop");

  fbl::AutoLock guard(&lock_);
  client_.clear();
}

zx_status_t AcpiPwrbtnDevice::HidbusGetDescriptor(hid_description_type_t desc_type,
                                                  uint8_t* out_data_buffer, size_t data_size,
                                                  size_t* out_data_actual) {
  zxlogf(DEBUG, "acpi-pwrbtn: hid bus get descriptor");

  if (out_data_buffer == nullptr || out_data_actual == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (desc_type != HID_DESCRIPTION_TYPE_REPORT) {
    return ZX_ERR_NOT_FOUND;
  }

  if (data_size < kHidDescriptorLen) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  memcpy(out_data_buffer, kHidDescriptor, kHidDescriptorLen);
  *out_data_actual = kHidDescriptorLen;

  return ZX_OK;
}

zx_status_t AcpiPwrbtnDevice::HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, uint8_t* data,
                                              size_t len, size_t* out_len) {
  if (out_len == NULL) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (rpt_type != HID_REPORT_TYPE_INPUT || rpt_id != 0) {
    return ZX_ERR_NOT_FOUND;
  }

  if (len < kHidReportLen) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  fbl::AutoLock guard(&lock_);
  uint8_t report = pressed_;
  static_assert(sizeof(report) == kHidReportLen, "");
  memcpy(data, &report, kHidReportLen);

  *out_len = kHidReportLen;
  return ZX_OK;
}

zx_status_t AcpiPwrbtnDevice::HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const uint8_t* data,
                                              size_t len) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AcpiPwrbtnDevice::HidbusGetIdle(uint8_t rpt_id, uint8_t* duration) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AcpiPwrbtnDevice::HidbusSetIdle(uint8_t rpt_id, uint8_t duration) { return ZX_OK; }

zx_status_t AcpiPwrbtnDevice::HidbusGetProtocol(uint8_t* protocol) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t AcpiPwrbtnDevice::HidbusSetProtocol(uint8_t protocol) { return ZX_OK; }

void AcpiPwrbtnDevice::DdkRelease() {
  zxlogf(INFO, "acpi-pwrbtn: DdkRelease");
  delete this;
}

zx_status_t AcpiPwrbtnDevice::Create(zx_device_t* parent, std::unique_ptr<AcpiPwrbtnDevice>* out) {
  fbl::AllocChecker ac;
  std::unique_ptr<AcpiPwrbtnDevice> dev(new (&ac) AcpiPwrbtnDevice(parent));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  ACPI_STATUS status =
      AcpiInstallFixedEventHandler(ACPI_EVENT_POWER_BUTTON, FixedEventHandler, dev.get());
  if (status != AE_OK) {
    // The dtor for AcpiPwrbtnDevice will clean these global handlers up when we
    // return here.
    return acpi_to_zx_status(status);
  }

  status = AcpiInstallNotifyHandler(ACPI_ROOT_OBJECT, ACPI_SYSTEM_NOTIFY | ACPI_DEVICE_NOTIFY,
                                    NotifyHandler, dev.get());
  if (status != AE_OK) {
    // The dtor for AcpiPwrbtnDevice will clean these global handlers up when we
    // return here.
    return acpi_to_zx_status(status);
  }

  *out = std::move(dev);
  return ZX_OK;
}

zx_status_t pwrbtn_init(zx_device_t* parent) {
  zxlogf(DEBUG, "acpi-pwrbtn: init");

  std::unique_ptr<AcpiPwrbtnDevice> dev;
  zx_status_t status = AcpiPwrbtnDevice::Create(parent, &dev);
  if (status != ZX_OK) {
    return status;
  }

  status = dev->DdkAdd("acpi-pwrbtn");
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the memory for dev
  __UNUSED auto ptr = dev.release();

  zxlogf(INFO, "acpi-pwrbtn: initialized");
  return ZX_OK;
}
