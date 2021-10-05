// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_CROS_EC_MOTION_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_CROS_EC_MOTION_H_

#include <fuchsia/hardware/hidbus/cpp/banjo.h>
#include <lib/stdcompat/span.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <memory>

#include <acpica/acpi.h>
#include <chromiumos-platform-ec/ec_commands.h>
#include <ddktl/device.h>
#include <fbl/array.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>

#include "acpi.h"
#include "dev.h"

namespace cros_ec {

class EmbeddedController;
class AcpiCrOsEcMotionDevice;
using DeviceType = ddk::Device<AcpiCrOsEcMotionDevice>;

// Properties for a single MotionSense sensor.
struct SensorInfo {
  bool valid;

  enum motionsensor_type type;
  enum motionsensor_location loc;
  uint32_t min_sampling_freq;
  uint32_t max_sampling_freq;
  uint32_t fifo_max_event_count;

  // For MOTIONSENSE_TYPE_ACCEL, value is in Gs
  //     MOTIONSENSE_TYPE_GYRO, value is in deg/s
  //     MOTIONSENSE_TYPE_MAG, value is in multiples of 1/16 uT
  //     MOTIONSENSE_TYPE_LIGHT, value is in lux?
  int32_t phys_min;
  int32_t phys_max;
};

// CrOS EC protocol to HID protocol translator for device motion sensors
class AcpiCrOsEcMotionDevice
    : public DeviceType,
      public ddk::HidbusProtocol<AcpiCrOsEcMotionDevice, ddk::base_protocol> {
 public:
  ~AcpiCrOsEcMotionDevice();

  // Create and bind the device.
  //
  // A pointer to the created device will be placed in |device|, though ownership
  // remains with the DDK. Any use of |device| must occur before DdkRelease()
  // is called.
  static zx_status_t Bind(zx_device_t* parent, fbl::RefPtr<EmbeddedController> ec,
                          std::unique_ptr<AcpiHandle> acpi_handle, AcpiCrOsEcMotionDevice** device);

  // DDK implementation.
  void DdkRelease();

  // hidbus protocol implementation.
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

 private:
  AcpiCrOsEcMotionDevice(fbl::RefPtr<EmbeddedController> ec, zx_device_t* parent,
                         std::unique_ptr<AcpiHandle> acpi_handle);
  DISALLOW_COPY_ASSIGN_AND_MOVE(AcpiCrOsEcMotionDevice);

  static void NotifyHandler(ACPI_HANDLE handle, UINT32 value, void* ctx);

  // Hardware commands
  zx_status_t QueryNumSensors(uint8_t* count);
  zx_status_t QuerySensorInfo(uint8_t sensor_num, SensorInfo* info);
  zx_status_t SetEcSamplingRate(uint8_t sensor_num, uint32_t milliseconds);
  zx_status_t SetSensorOutputDataRate(uint8_t sensor_num, uint32_t freq_millihertz);
  zx_status_t GetSensorRange(uint8_t sensor_num, int32_t* range);
  zx_status_t GetKbWakeAngle(int32_t* angle);
  zx_status_t SetKbWakeAngle(int16_t angle);
  zx_status_t FifoInterruptEnable(bool enable);
  zx_status_t FifoRead(struct ec_response_motion_sensor_data* data);

  // Guard against concurrent use of the HID interfaces
  fbl::Mutex hid_lock_;
  void QueueHidReportLocked(const uint8_t* data, size_t len);
  zx_status_t ConsumeFifoLocked();

  // Chat with hardware to build up |sensors_|
  zx_status_t ProbeSensors();

  fbl::RefPtr<EmbeddedController> ec_;

  std::unique_ptr<AcpiHandle> acpi_handle_;

  // Interface the driver is currently bound to
  ddk::HidbusIfcProtocolClient client_;

  fbl::Vector<SensorInfo> sensors_;

  fbl::Array<uint8_t> hid_descriptor_ = fbl::Array<uint8_t>();
};

// Build a HID descriptor reporting information about the given set of sensors.
zx_status_t BuildHidDescriptor(cpp20::span<const SensorInfo> sensors, fbl::Array<uint8_t>* result);

}  // namespace cros_ec

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_CROS_EC_MOTION_H_
