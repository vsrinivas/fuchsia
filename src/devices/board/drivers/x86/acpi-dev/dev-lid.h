// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_LID_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_LID_H_

#include <fuchsia/hardware/hidbus/cpp/banjo.h>
#include <inttypes.h>
#include <lib/fit/function.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <memory>

#include <acpica/acpi.h>
#include <ddktl/device.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

namespace acpi_lid {

enum class LidState {
  UNKNOWN = -1,  // No observation has been made yet
  CLOSED = 0,
  OPEN = 1
};

// function pointers for testability, used to mock out ACPI methods where necessary
using AcpiObjectEvalFunc = fit::function<ACPI_STATUS(ACPI_HANDLE, ACPI_STRING, ACPI_OBJECT_LIST*,
                                                     ACPI_BUFFER*, ACPI_OBJECT_TYPE)>;
using AcpiInstallNotifyHandlerFunc = fit::function<ACPI_STATUS(
    ACPI_HANDLE handle, UINT32 handler_type, ACPI_NOTIFY_HANDLER handler, void* ctx)>;
using AcpiRemoveNotifyHandlerFunc = fit::function<ACPI_STATUS(
    ACPI_HANDLE handle, UINT32 handler_type, ACPI_NOTIFY_HANDLER handler)>;

class AcpiLidDevice;
using DeviceType = ddk::Device<AcpiLidDevice, ddk::Unbindable>;

// An instance of a PNP0C0D Lid device. It presents a HID interface with a single input, the state
// of the lid switch.
class AcpiLidDevice : public DeviceType,
                      public ddk::HidbusProtocol<AcpiLidDevice, ddk::base_protocol> {
 public:
  AcpiLidDevice(zx_device_t* parent, ACPI_HANDLE acpi_handle, AcpiObjectEvalFunc acpi_eval,
                AcpiInstallNotifyHandlerFunc acpi_install_notify,
                AcpiRemoveNotifyHandlerFunc acpi_remove_notify)
      : DeviceType(parent),
        acpi_handle_(acpi_handle),
        acpi_eval_(std::move(acpi_eval)),
        acpi_install_notify_(std::move(acpi_install_notify)),
        acpi_remove_notify_(std::move(acpi_remove_notify)) {}

  static zx_status_t Create(zx_device_t* parent, ACPI_HANDLE acpi_handle,
                            std::unique_ptr<AcpiLidDevice>* out) {
    return Create(parent, acpi_handle, out, AcpiEvaluateObjectTyped, AcpiInstallNotifyHandler,
                  AcpiRemoveNotifyHandler);
  }

  // Exposed for testing
  static zx_status_t Create(zx_device_t* parent, ACPI_HANDLE acpi_handle,
                            std::unique_ptr<AcpiLidDevice>* out, AcpiObjectEvalFunc acpi_eval,
                            AcpiInstallNotifyHandlerFunc acpi_install_notify,
                            AcpiRemoveNotifyHandlerFunc acpi_remove_notify);

  LidState State() {
    fbl::AutoLock guard(&lock_);
    return lid_state_;
  }

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

  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  static const uint8_t kHidDescriptor[];
  static const size_t kHidDescriptorLen;
  static constexpr size_t kHidReportLen = 1;

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(AcpiLidDevice);

  static void NotifyHandler(ACPI_HANDLE handle, UINT32 value, void* ctx);

  zx_status_t UpdateLidStateLocked() TA_REQ(lock_);
  void QueueHidReportLocked() TA_REQ(lock_);

  void PublishLidStateIfChanged();

  const ACPI_HANDLE acpi_handle_;

  fbl::Mutex lock_;

  // Current state of the lid switch
  LidState lid_state_ TA_GUARDED(lock_) = LidState::UNKNOWN;

  // Interface the driver is currently bound to
  ddk::HidbusIfcProtocolClient client_ TA_GUARDED(lock_);

  const AcpiObjectEvalFunc acpi_eval_;
  const AcpiInstallNotifyHandlerFunc acpi_install_notify_;
  const AcpiRemoveNotifyHandlerFunc acpi_remove_notify_;
};

}  // namespace acpi_lid

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_LID_H_
