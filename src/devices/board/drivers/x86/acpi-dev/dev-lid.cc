// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dev-lid.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <utility>

#include <acpica/acpi.h>
#include <ddk/debug.h>
#include <ddktl/device.h>
#include <ddktl/protocol/hidbus.h>
#include <fbl/auto_lock.h>
#include <hid/descriptor.h>

#include "dev.h"
#include "errors.h"

namespace {

constexpr static acpi_lid::LidState LidStateFromAcpiValue(uint64_t value) {
  return value ? acpi_lid::LidState::OPEN : acpi_lid::LidState::CLOSED;
}

constexpr static uint8_t LidStateToHidReport(acpi_lid::LidState state) {
  ZX_DEBUG_ASSERT(state != acpi_lid::LidState::UNKNOWN);
  return static_cast<uint8_t>(state);
}

}  // namespace

namespace acpi_lid {

// We encode the lid switch events as a vendor-defined System Control.
// This is a bit hacky, but there is no lid switch defined in the HID usage
// tables.
// System Control collections are meant to be consumed by the
// operating system, not user applications.
const uint8_t AcpiLidDevice::kHidDescriptor[] = {
    HID_USAGE_PAGE(0x01),  // Usage Page (Generic Desktop)
    HID_USAGE(0x80),       // Usage (System Control)
    HID_COLLECTION_APPLICATION,
    HID_USAGE16(0x01FF),  // Usage (Vendor defined)
    HID_LOGICAL_MIN(0),
    HID_LOGICAL_MAX(1),
    HID_REPORT_COUNT(1),
    HID_REPORT_SIZE(1),  // 1 bit for lid state
    HID_INPUT(0x02),     // Input (Data,Var,Abs)
    HID_REPORT_SIZE(7),  // 7 bits of padding
    HID_INPUT(0x03),     // Input (Const,Var,Abs)
    HID_END_COLLECTION,
};

const size_t AcpiLidDevice::kHidDescriptorLen = sizeof(AcpiLidDevice::kHidDescriptor);

zx_status_t AcpiLidDevice::UpdateLidStateLocked() TA_REQ(lock_) {
  ACPI_OBJECT obj = {};
  ACPI_BUFFER buffer = {
      .Length = sizeof(obj),
      .Pointer = &obj,
  };
  ACPI_STATUS acpi_status =
      acpi_eval_(acpi_handle_, const_cast<char*>("_LID"), nullptr, &buffer, ACPI_TYPE_INTEGER);
  if (acpi_status != AE_OK) {
    zx_status_t status = acpi_to_zx_status(acpi_status);
    zxlogf(ERROR, "acpi-lid: _LID failed: %d (%s)", acpi_status, zx_status_get_string(status));
    return status;
  }
  zxlogf(DEBUG, "acpi-lid: _LID returned 0x%llx", obj.Integer.Value);

  lid_state_ = LidStateFromAcpiValue(obj.Integer.Value);
  zxlogf(DEBUG, "acpi-lid: Lid is %s", (lid_state_ == LidState::OPEN ? "open" : "closed"));
  return ZX_OK;
}

void AcpiLidDevice::QueueHidReportLocked() TA_REQ(lock_) {
  if (client_.is_valid()) {
    zxlogf(DEBUG, "acpi-lid: queueing report");
    uint8_t report = LidStateToHidReport(lid_state_);
    client_.IoQueue(&report, sizeof(report), zx_clock_get_monotonic());
  }
}

void AcpiLidDevice::PublishLidStateIfChanged() {
  fbl::AutoLock guard(&lock_);
  LidState old_state = lid_state_;
  if (UpdateLidStateLocked() == ZX_OK && old_state != lid_state_) {
    QueueHidReportLocked();
  }
}

void AcpiLidDevice::NotifyHandler(ACPI_HANDLE handle, UINT32 value, void* ctx) {
  auto dev = reinterpret_cast<AcpiLidDevice*>(ctx);
  zxlogf(DEBUG, "acpi-lid: got event 0x%x", value);
  if (value == 0x80) {
    // Lid state has changed
    dev->PublishLidStateIfChanged();
  }
}

zx_status_t AcpiLidDevice::HidbusQuery(uint32_t options, hid_info_t* info) {
  zxlogf(DEBUG, "acpi-lid: hid bus query");

  info->dev_num = 0;
  info->device_class = HID_DEVICE_CLASS_OTHER;
  info->boot_device = false;
  return ZX_OK;
}

zx_status_t AcpiLidDevice::HidbusStart(const hidbus_ifc_protocol_t* ifc) {
  zxlogf(DEBUG, "acpi-lid: hid bus start");

  fbl::AutoLock guard(&lock_);
  if (client_.is_valid()) {
    return ZX_ERR_ALREADY_BOUND;
  }
  auto acpi_status = acpi_install_notify_(acpi_handle_, ACPI_DEVICE_NOTIFY, NotifyHandler, this);
  if (acpi_status != AE_OK) {
    auto status = acpi_to_zx_status(acpi_status);
    zxlogf(ERROR, "acpi-lid: Failed to install notify handler: %d (%s)", acpi_status,
           zx_status_get_string(status));
    return status;
  }
  client_ = ddk::HidbusIfcProtocolClient(ifc);
  return ZX_OK;
}

void AcpiLidDevice::HidbusStop() {
  zxlogf(DEBUG, "acpi-lid: hid bus stop");

  fbl::AutoLock guard(&lock_);
  ACPI_STATUS acpi_status;
  if ((acpi_status = acpi_remove_notify_(acpi_handle_, ACPI_DEVICE_NOTIFY, NotifyHandler)) !=
      AE_OK) {
    zxlogf(ERROR, "acpi-lid: Failed to uninstall notify handler: %d (%s)", acpi_status,
           zx_status_get_string(acpi_to_zx_status(acpi_status)));
  }
  client_.clear();
}

zx_status_t AcpiLidDevice::HidbusGetDescriptor(hid_description_type_t desc_type,
                                               void* out_data_buffer, size_t data_size,
                                               size_t* out_data_actual) {
  zxlogf(DEBUG, "acpi-lid: hid bus get descriptor");

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

zx_status_t AcpiLidDevice::HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len,
                                           size_t* out_len) {
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
  uint8_t report = LidStateToHidReport(lid_state_);
  static_assert(sizeof(report) == kHidReportLen);
  memcpy(data, &report, kHidReportLen);

  *out_len = kHidReportLen;
  return ZX_OK;
}

zx_status_t AcpiLidDevice::HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const void* data,
                                           size_t len) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AcpiLidDevice::HidbusGetIdle(uint8_t rpt_id, uint8_t* duration) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AcpiLidDevice::HidbusSetIdle(uint8_t rpt_id, uint8_t duration) { return ZX_OK; }

zx_status_t AcpiLidDevice::HidbusGetProtocol(uint8_t* protocol) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t AcpiLidDevice::HidbusSetProtocol(uint8_t protocol) { return ZX_OK; }

void AcpiLidDevice::DdkUnbind(ddk::UnbindTxn txn) {
  zxlogf(INFO, "acpi-lid: unbind");
  txn.Reply();
}

void AcpiLidDevice::DdkRelease() {
  zxlogf(INFO, "acpi-lid: release");
  delete this;
}

zx_status_t AcpiLidDevice::Create(zx_device_t* parent, ACPI_HANDLE acpi_handle,
                                  std::unique_ptr<AcpiLidDevice>* out, AcpiObjectEvalFunc acpi_eval,
                                  AcpiInstallNotifyHandlerFunc acpi_install_notify,
                                  AcpiRemoveNotifyHandlerFunc acpi_remove_notify) {
  auto dev = std::make_unique<AcpiLidDevice>(parent, acpi_handle, std::move(acpi_eval),
                                             std::move(acpi_install_notify),
                                             std::move(acpi_remove_notify));
  // Initialize tracked state
  {
    fbl::AutoLock guard(&dev->lock_);
    dev->UpdateLidStateLocked();
  }

  *out = std::move(dev);
  return ZX_OK;
}

}  // namespace acpi_lid

zx_status_t lid_init(zx_device_t* parent, ACPI_HANDLE acpi_handle) {
  zxlogf(DEBUG, "acpi-lid: init");

  std::unique_ptr<acpi_lid::AcpiLidDevice> dev;
  zx_status_t status = acpi_lid::AcpiLidDevice::Create(parent, acpi_handle, &dev);
  if (status != ZX_OK) {
    return status;
  }

  status = dev->DdkAdd("acpi-lid");
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the memory for dev
  __UNUSED auto* ptr = dev.release();

  zxlogf(INFO, "acpi-lid: initialized");
  return ZX_OK;
}
