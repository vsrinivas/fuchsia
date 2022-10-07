// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/acpi/drivers/acpi-lid/acpi_lid.h"

#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>
#include <fuchsia/hardware/hidbus/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/zx/clock.h>

#include <hid/descriptor.h>

#include "lib/ddk/device.h"
#include "src/devices/acpi/drivers/acpi-lid/acpi_lid-bind.h"
#include "src/devices/lib/acpi/client.h"

namespace acpi_lid {
namespace facpi = fuchsia_hardware_acpi::wire;

namespace {

constexpr static uint8_t LidStateToHidReport(LidState state) {
  ZX_DEBUG_ASSERT(state != LidState::kUnknown);
  return static_cast<uint8_t>(state);
}

}  // namespace

// We encode the lid switch events as a vendor-defined System Control.
// This is a bit hacky, but there is no lid switch defined in the HID usage tables.
// System Control collections are meant to be consumed by the operating system, not user
// applications.
const uint8_t AcpiLid::kHidDescriptor[] = {
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

const size_t AcpiLid::kHidDescriptorLen = sizeof(AcpiLid::kHidDescriptor);

zx_status_t AcpiLid::Bind(void* ctx, zx_device_t* dev) {
  auto acpi = acpi::Client::Create(dev);
  if (acpi.is_error()) {
    zxlogf(ERROR, "Failed to create ACPI client: %s", zx_status_get_string(acpi.error_value()));
    return acpi.error_value();
  }

  async_dispatcher_t* dispatcher = fdf::Dispatcher::GetCurrent()->async_dispatcher();
  auto lid_device = std::make_unique<AcpiLid>(dev, std::move(acpi.value()), dispatcher);
  zx_status_t status = lid_device->Bind();
  if (status == ZX_OK) {
    // The DDK takes ownership of the device.
    __UNUSED auto unused = lid_device.release();
  }

  return status;
}

zx_status_t AcpiLid::Bind() { return DdkAdd(ddk::DeviceAddArgs("acpi_lid")); }

void AcpiLid::DdkInit(ddk::InitTxn txn) {
  // Initialise the lid state.
  std::scoped_lock lock(lock_);
  UpdateLidStateLocked();

  txn.Reply(ZX_OK);
}

void AcpiLid::DdkSuspend(ddk::SuspendTxn txn) {
  if ((txn.suspend_reason() & DEVICE_MASK_SUSPEND_REASON) != DEVICE_SUSPEND_REASON_SUSPEND_RAM) {
    txn.Reply(ZX_OK, txn.requested_state());
    return;
  }

  auto result = acpi_.borrow()->SetWakeDevice(txn.requested_state());
  if (!result.ok()) {
    zxlogf(ERROR, "Failed to set lid as a wake device: %d", result.status());
  }

  txn.Reply(ZX_OK, txn.requested_state());
}

void AcpiLid::DdkRelease() { delete this; }

zx_status_t AcpiLid::HidbusQuery(uint32_t options, hid_info_t* info) {
  info->dev_num = 0;
  info->device_class = HID_DEVICE_CLASS_OTHER;
  info->boot_device = false;
  return ZX_OK;
}

zx_status_t AcpiLid::HidbusStart(const hidbus_ifc_protocol_t* ifc) {
  std::scoped_lock lock(lock_);
  if (client_.is_valid()) {
    return ZX_ERR_ALREADY_BOUND;
  }

  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_acpi::NotifyHandler>();
  if (endpoints.is_error()) {
    return endpoints.error_value();
  }

  fidl::BindServer<fidl::WireServer<fuchsia_hardware_acpi::NotifyHandler>>(
      dispatcher_, std::move(endpoints->server), this);

  auto result = acpi_.borrow()->InstallNotifyHandler(facpi::NotificationMode::kDevice,
                                                     std::move(endpoints->client));
  if (!result.ok()) {
    return result.status();
  }

  if (result.value().is_error()) {
    return ZX_ERR_INTERNAL;
  }

  client_ = ddk::HidbusIfcProtocolClient(ifc);
  return ZX_OK;
}

void AcpiLid::HidbusStop() {
  std::scoped_lock lock(lock_);

  // This method never returns an error so we don't check the result.
  auto result = acpi_.borrow()->RemoveNotifyHandler();

  client_.clear();
}

zx_status_t AcpiLid::HidbusGetDescriptor(hid_description_type_t desc_type, uint8_t* out_data_buffer,
                                         size_t data_size, size_t* out_data_actual) {
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

zx_status_t AcpiLid::HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, uint8_t* data, size_t len,
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

  std::scoped_lock lock(lock_);
  uint8_t report = LidStateToHidReport(lid_state_);
  static_assert(sizeof(report) == kHidReportLen);
  memcpy(data, &report, kHidReportLen);

  *out_len = kHidReportLen;
  return ZX_OK;
}

void AcpiLid::Handle(HandleRequestView request, HandleCompleter::Sync& completer) {
  if (request->value == kLidStateChange) {
    PublishLidStateIfChanged();
  }
  completer.Reply();
}

void AcpiLid::PublishLidStateIfChanged() {
  std::scoped_lock lock(lock_);
  LidState old_state = lid_state_;
  if (UpdateLidStateLocked() == ZX_OK && old_state != lid_state_) {
    QueueHidReportLocked();
  }
}

zx_status_t AcpiLid::UpdateLidStateLocked() {
  auto result = acpi_.borrow()->EvaluateObject("_LID", facpi::EvaluateObjectMode::kPlainObject, {});
  if (!result.ok()) {
    zxlogf(ERROR, "EvaluateObject FIDL call failed: %s", zx_status_get_string(result.status()));
    return result.status();
  }

  if (result.value().is_error()) {
    zxlogf(ERROR, "EvaluateObject failed: %d", static_cast<uint32_t>(result.value().error_value()));
    return ZX_ERR_INTERNAL;
  }

  fidl::WireOptional<facpi::EncodedObject>& maybe_encoded = result->value()->result;
  if (!maybe_encoded.has_value() || !maybe_encoded->is_object() ||
      !maybe_encoded->object().is_integer_val()) {
    zxlogf(ERROR, "Unexpected response from EvaluateObject");
    return ZX_ERR_INTERNAL;
  }

  uint64_t int_lid_state = maybe_encoded->object().integer_val();
  lid_state_ = int_lid_state ? LidState::kOpen : LidState::kClosed;

  return ZX_OK;
}

void AcpiLid::QueueHidReportLocked() {
  if (client_.is_valid()) {
    zxlogf(DEBUG, "Queueing report: lid is %s",
           (lid_state_ == LidState::kOpen ? "open" : "closed"));
    uint8_t report = LidStateToHidReport(lid_state_);
    client_.IoQueue(&report, sizeof(report), zx_clock_get_monotonic());
  }
}

static zx_driver_ops_t acpi_lid_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = AcpiLid::Bind,
};

}  // namespace acpi_lid

// clang-format off
ZIRCON_DRIVER(acpi-lid, acpi_lid::acpi_lid_driver_ops, "zircon", "0.1");
