// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MCU_DRIVERS_CHROMIUMOS_EC_CORE_MOTION_H_
#define SRC_DEVICES_MCU_DRIVERS_CHROMIUMOS_EC_CORE_MOTION_H_

#include <fuchsia/hardware/hidbus/cpp/banjo.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <memory>

#include <chromiumos-platform-ec/ec_commands.h>
#include <ddktl/device.h>
#include <fbl/array.h>
#include <fbl/mutex.h>

#include "src/devices/mcu/drivers/chromiumos-ec-core/chromiumos_ec_core.h"

namespace chromiumos_ec_core::motion {

class AcpiCrOsEcMotionDevice;
using DeviceType = ddk::Device<AcpiCrOsEcMotionDevice, ddk::Initializable>;

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
  ~AcpiCrOsEcMotionDevice() = default;

  // Create and bind the device.
  //
  // A pointer to the created device will be placed in |device|, though ownership
  // remains with the DDK. Any use of |device| must occur before DdkRelease()
  // is called.
  static zx_status_t Bind(zx_device_t* parent, ChromiumosEcCore* ec,
                          AcpiCrOsEcMotionDevice** device);

  // DDK implementation.
  void DdkInit(ddk::InitTxn txn);
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

  // ACPI Notifications
  void HandleNotify(uint32_t event);

 private:
  AcpiCrOsEcMotionDevice(ChromiumosEcCore* ec, zx_device_t* parent);
  DISALLOW_COPY_ASSIGN_AND_MOVE(AcpiCrOsEcMotionDevice);

  // Hardware commands
  fpromise::promise<uint8_t, zx_status_t> QueryNumSensors();
  fpromise::promise<SensorInfo, zx_status_t> QuerySensorInfo(uint8_t sensor_num);
  zx_status_t SetEcSamplingRate(uint8_t sensor_num, uint32_t milliseconds);
  zx_status_t SetSensorOutputDataRate(uint8_t sensor_num, uint32_t freq_millihertz);
  fpromise::promise<int32_t, zx_status_t> GetSensorRange(uint8_t sensor_num);
  zx_status_t FifoInterruptEnable(bool enable);
  fpromise::promise<ec_response_motion_sensor_data, zx_status_t> FifoRead();

  // Guard against concurrent use of the HID interfaces
  fbl::Mutex hid_lock_;
  void QueueHidReportLocked(const uint8_t* data, size_t len);
  void ConsumeFifoAsync(bool enabling);

  // Chat with hardware to build up |sensors_|
  zx_status_t ProbeSensors();

  ChromiumosEcCore* ec_;

  // Interface the driver is currently bound to
  ddk::HidbusIfcProtocolClient client_;

  std::vector<SensorInfo> sensors_;

  fbl::Array<uint8_t> hid_descriptor_ = fbl::Array<uint8_t>();
  std::optional<ddk::InitTxn> init_txn_;
  std::optional<ChromiumosEcCore::NotifyHandlerDeleter> notify_deleter_;
};

// Build a HID descriptor reporting information about the given set of sensors.
zx_status_t BuildHidDescriptor(cpp20::span<const SensorInfo> sensors, fbl::Array<uint8_t>* result);

}  // namespace chromiumos_ec_core::motion

#endif  // SRC_DEVICES_MCU_DRIVERS_CHROMIUMOS_EC_CORE_MOTION_H_
