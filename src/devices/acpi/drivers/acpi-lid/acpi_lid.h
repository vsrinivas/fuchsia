// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_ACPI_DRIVERS_ACPI_LID_ACPI_LID_H_
#define SRC_DEVICES_ACPI_DRIVERS_ACPI_LID_ACPI_LID_H_

#include <fuchsia/hardware/hidbus/cpp/banjo.h>

#include <ddktl/device.h>

#include "src/devices/lib/acpi/client.h"

namespace acpi_lid {

inline constexpr uint32_t kLidStateChange = 0x80;

enum class LidState {
  kUnknown = -1,  // No observation has been made yet
  kClosed = 0,
  kOpen = 1
};

class AcpiLid;
using DeviceType = ddk::Device<AcpiLid, ddk::Initializable, ddk::Suspendable>;

// An instance of a PNP0C0D Lid device. It presents a HID interface with a single input, the state
// of the lid switch.
class AcpiLid : public DeviceType,
                public ddk::HidbusProtocol<AcpiLid, ddk::base_protocol>,
                public fidl::WireServer<fuchsia_hardware_acpi::NotifyHandler> {
 public:
  explicit AcpiLid(zx_device_t* parent, acpi::Client acpi, async_dispatcher_t* dispatcher)
      : DeviceType(parent), acpi_(std::move(acpi)), dispatcher_(dispatcher) {}

  // Hidbus Protocol functions
  zx_status_t HidbusQuery(uint32_t options, hid_info_t* info);
  zx_status_t HidbusStart(const hidbus_ifc_protocol_t* ifc);
  void HidbusStop();
  zx_status_t HidbusGetDescriptor(hid_description_type_t desc_type, uint8_t* out_data_buffer,
                                  size_t data_size, size_t* out_data_actual);
  zx_status_t HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, uint8_t* data, size_t len,
                              size_t* out_len);

  // Unsupported Hidbus functions
  zx_status_t HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const uint8_t* data, size_t len) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t HidbusGetIdle(uint8_t rpt_id, uint8_t* duration) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t HidbusSetIdle(uint8_t rpt_id, uint8_t duration) { return ZX_OK; }
  zx_status_t HidbusGetProtocol(uint8_t* protocol) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t HidbusSetProtocol(uint8_t protocol) { return ZX_OK; }

  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  zx_status_t Bind();
  void DdkInit(ddk::InitTxn txn);
  void DdkSuspend(ddk::SuspendTxn txn);
  void DdkRelease();

  // ACPI NotifyHandler FIDL methods.
  void Handle(HandleRequestView request, HandleCompleter::Sync& completer) override;

  // Exposed for testing
  LidState State() {
    std::scoped_lock lock(lock_);
    return lid_state_;
  }

  static const uint8_t kHidDescriptor[];
  static const size_t kHidDescriptorLen;
  static constexpr size_t kHidReportLen = 1;

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(AcpiLid);

  acpi::Client acpi_;
  async_dispatcher_t* dispatcher_ = nullptr;
  std::mutex lock_;
  LidState lid_state_ __TA_GUARDED(lock_) = LidState::kUnknown;
  ddk::HidbusIfcProtocolClient client_ __TA_GUARDED(lock_);

  void PublishLidStateIfChanged();
  zx_status_t UpdateLidStateLocked() __TA_REQUIRES(lock_);
  void QueueHidReportLocked() __TA_REQUIRES(lock_);
};

}  // namespace acpi_lid

#endif  // SRC_DEVICES_ACPI_DRIVERS_ACPI_LID_ACPI_LID_H_
