// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_SENSORS_IMX355_IMX355_H_
#define SRC_CAMERA_DRIVERS_SENSORS_IMX355_IMX355_H_

#include <lib/device-protocol/i2c-channel.h>
#include <lib/device-protocol/pdev.h>
#include <lib/fit/result.h>

#include <array>
#include <atomic>
#include <mutex>

#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/protocol/camera/sensor.h>
#include <ddktl/protocol/camerasensor.h>
#include <ddktl/protocol/clock.h>
#include <ddktl/protocol/composite.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/mipicsi.h>
#ifndef _ALL_SOURCE
#define _ALL_SOURCE  // Enables thrd_create_with_name in <threads.h>.
#endif
#include <threads.h>

namespace camera {

// TODO(57529): Refactor into a class that incorporates relevant methods.
// TODO(jsasinowski): Generalize to cover additional gain modes.
struct AnalogGain {
  // Analog gain constants queried from the sensor, per the SMIA spec.
  //     gain = (m0 * x + c0) / (m1 * x + c1)
  // Exactly one of m0 and m1 must be 0.
  int16_t m0_;
  int16_t c0_;
  int16_t m1_;
  int16_t c1_;
  uint16_t gain_code_min_;
  uint16_t gain_code_max_;
  uint16_t gain_code_step_size_;

  // Flag to indicate when an update is needed.
  bool update_gain_;
  uint16_t gain_code_global_;
};

// TODO(jsasinowski): Refactor into a class that incorporates relevant methods.
struct DigitalGain {
  uint16_t gain_min_;
  uint16_t gain_max_;
  uint16_t gain_step_size_;

  // Flag to indicate when an update is needed.
  bool update_gain_;
  uint16_t gain_;
};

// TODO(jsasinowski): Refactor into a class that incorporates relevant methods.
struct IntegrationTime {
  // Flag to indicate when an update is needed.
  bool update_integration_time_;
  uint16_t coarse_integration_time_;
};

class Imx355Device;
using DeviceType = ddk::Device<Imx355Device, ddk::UnbindableNew>;

class Imx355Device : public DeviceType,
                     public ddk::CameraSensor2Protocol<Imx355Device, ddk::base_protocol> {
 public:
  enum {
    FRAGMENT_PDEV,
    FRAGMENT_MIPICSI,
    FRAGMENT_I2C,
    FRAGMENT_GPIO_CAM_MUTE,
    FRAGMENT_GPIO_CAM_RST,
    FRAGMENT_GPIO_VANA,
    FRAGMENT_GPIO_VDIG,
    FRAGMENT_GPIO_VFIF,
    FRAGMENT_CLK24,
    FRAGMENT_COUNT,
  };

  Imx355Device(zx_device_t* device, zx_device_t* i2c, zx_device_t* gpio_cam_mute,
               zx_device_t* gpio_cam_rst, zx_device_t* gpio_vana, zx_device_t* gpio_vdig,
               zx_device_t* gpio_vfif, zx_device_t* clk24, zx_device_t* mipicsi)
      : DeviceType(device),
        i2c_(i2c),
        gpio_cam_mute_(gpio_cam_mute),
        gpio_cam_rst_(gpio_cam_rst),
        gpio_vana_enable_(gpio_vana),
        gpio_vdig_enable_(gpio_vdig),
        gpio_vfif_enable_(gpio_vfif),

        clk24_(clk24),
        mipi_(mipicsi) {}

  static zx_status_t Create(zx_device_t* parent, std::unique_ptr<Imx355Device>* device_out);
  static zx_status_t CreateAndBind(void* ctx, zx_device_t* parent);
  static bool RunUnitTests(void* ctx, zx_device_t* parent, zx_handle_t channel);

  // Methods required by the ddk mixins
  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease();

  bool IsSensorOutOfReset() {
    std::lock_guard guard(lock_);
    return ValidateSensorID();
  }

  // OTP

  //  Read the sensor's entire OTP memory.
  //
  //  Returns:
  //    A result with a vmo containing the OTP blob if the read succeeded. Otherwise returns a
  //    result with an error code.
  fit::result<zx::vmo, zx_status_t> OtpRead();

  //  Validates the integrity of the data written to the OTP. A checksum is calculated from the
  //  written data and checked against a hard-coded value.
  //
  //  Args:
  //    |vmo|   VMO of data to be validated
  //
  //  Returns:
  //    Whether the OTP data validated successfully.
  static bool OtpValidate(const zx::vmo& vmo);

  // |ZX_PROTOCOL_CAMERA_SENSOR2|
  zx_status_t CameraSensor2Init();
  void CameraSensor2DeInit();
  zx_status_t CameraSensor2GetSensorId(uint32_t* out_id);
  zx_status_t CameraSensor2GetAvailableModes(operating_mode_t* out_modes_list, size_t modes_count,
                                             size_t* out_modes_actual);
  zx_status_t CameraSensor2SetMode(uint32_t mode);
  zx_status_t CameraSensor2StartStreaming();
  void CameraSensor2StopStreaming();
  zx_status_t CameraSensor2GetAnalogGain(float* out_gain);
  zx_status_t CameraSensor2SetAnalogGain(float gain, float* out_gain);
  zx_status_t CameraSensor2GetDigitalGain(float* out_gain);
  zx_status_t CameraSensor2SetDigitalGain(float gain, float* out_gain);
  zx_status_t CameraSensor2GetIntegrationTime(float* out_int_time);
  zx_status_t CameraSensor2SetIntegrationTime(float int_time, float* out_int_time);
  zx_status_t CameraSensor2Update();
  zx_status_t CameraSensor2GetOtpSize(uint32_t* out_size);
  zx_status_t CameraSensor2GetOtpData(uint32_t byte_count, uint32_t offset, zx::vmo* out_otp_data);
  zx_status_t CameraSensor2GetTestPatternMode(uint16_t* out_value);
  zx_status_t CameraSensor2SetTestPatternMode(uint16_t mode);
  zx_status_t CameraSensor2GetTestPatternData(color_val_t* out_data);
  zx_status_t CameraSensor2SetTestPatternData(const color_val_t* data);
  zx_status_t CameraSensor2GetTestCursorData(rect_t* out_data);
  zx_status_t CameraSensor2SetTestCursorData(const rect_t* data);
  zx_status_t CameraSensor2GetExtensionValue(uint64_t id, extension_value_data_type_t* out_value);
  zx_status_t CameraSensor2SetExtensionValue(uint64_t id, const extension_value_data_type_t* value,
                                             extension_value_data_type_t* out_value);

 protected:
  // Protocols
  ddk::I2cChannel i2c_;
  ddk::GpioProtocolClient gpio_cam_mute_;
  ddk::GpioProtocolClient gpio_cam_rst_;
  ddk::GpioProtocolClient gpio_vana_enable_;
  ddk::GpioProtocolClient gpio_vdig_enable_;
  ddk::GpioProtocolClient gpio_vfif_enable_;
  ddk::ClockProtocolClient clk24_;
  ddk::MipiCsiProtocolClient mipi_;

  // Other
  zx_status_t InitPdev();

 private:
  // I2C Helpers
  // Returns ZX_OK and an uint16_t value if the read succeeds.
  // Returns error if the I2C read fails.
  fit::result<uint16_t, zx_status_t> Read16(uint16_t addr) __TA_REQUIRES(lock_);
  // Returns ZX_OK and an uint8_t value if the read succeeds.
  // Returns error if the I2C read fails.
  fit::result<uint8_t, zx_status_t> Read8(uint16_t addr) __TA_REQUIRES(lock_);
  // Returns ZX_OK if the write is successful otherwise returns an error.
  zx_status_t Write16(uint16_t addr, uint16_t val) __TA_REQUIRES(lock_);
  // Returns ZX_OK if the write is successful otherwise returns an error.
  zx_status_t Write8(uint16_t addr, uint8_t val) __TA_REQUIRES(lock_);

  // Other
  zx_status_t InitMipiCsi(uint8_t mode) __TA_REQUIRES(lock_);
  zx_status_t InitSensor(uint8_t idx) __TA_REQUIRES(lock_);
  void HwInit() __TA_REQUIRES(lock_);
  void HwDeInit() __TA_REQUIRES(lock_);
  void ShutDown();
  bool ValidateSensorID() __TA_REQUIRES(lock_);

  bool is_streaming_;
  uint8_t num_modes_;
  uint8_t current_mode_;

  fit::result<uint8_t, zx_status_t> GetRegisterValueFromSequence(uint8_t index, uint16_t address);

  // Timing data
  fit::result<uint32_t, zx_status_t> GetLinesPerSecond();

  // Exposure data

  // Analog gain
  AnalogGain analog_gain_;
  zx_status_t ReadAnalogGainConstants() __TA_REQUIRES(lock_);
  float AnalogRegValueToTotalGain(uint16_t);
  uint16_t AnalogTotalGainToRegValue(float);

  // Digital gain
  DigitalGain digital_gain_;
  zx_status_t ReadDigitalGainConstants() __TA_REQUIRES(lock_);
  float DigitalRegValueToTotalGain(uint16_t);
  uint16_t DigitalTotalGainToRegValue(float);

  // Integration time
  IntegrationTime integration_time_;

  bool gain_constants_valid_ = false;
  zx_status_t ReadGainConstants() __TA_REQUIRES(lock_);

  zx_status_t SetGroupedParameterHold(bool) __TA_REQUIRES(lock_);

  std::mutex lock_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_SENSORS_IMX355_IMX355_H_
