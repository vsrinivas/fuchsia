// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_SENSORS_MOCK_SENSOR_MOCK_SENSOR_DEVICE_H_
#define SRC_CAMERA_DRIVERS_SENSORS_MOCK_SENSOR_MOCK_SENSOR_DEVICE_H_

#include <lib/fake_ddk/fake_ddk.h>

#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/protocol/camerasensor.h>

#include <vector>


namespace camera {

// This is a mock sensor driver.
// This driver exists as a way to easily plug in a fake sensor into Isp tests.
// It is an extremely light weight class and only provides very basic
// functionality. This should not be used for the purpose of testing sensors,
// but rather testing Isps that require a sensor for initialization.
//
// Example:
//   MockSensorDevice sensor = MockSensorDevice();
//   IspDevice isp = IspDevice(fake_ddk::kFakeParent, sensor.zxdev())
//   *Make some calls on isp that wrap sensor*
class MockSensorDevice;

using DeviceType = ddk::Device<MockSensorDevice>;

class MockSensorDevice
    : public DeviceType,
      public ddk::CameraSensorProtocol<MockSensorDevice, ddk::base_protocol> {
 public:
  MockSensorDevice() : DeviceType(fake_ddk::kFakeParent) {}

  // Methods required by ddk.
  void DdkRelease();

  // Methods required for ZX_PROTOCOL_CAMERA_SENSOR.

  // Initializes the sensor.
  zx_status_t CameraSensorInit();

  // Deinitializes the sensor.
  void CameraSensorDeInit();

  // Sets the current mode of the sensor to the mode index of supported modes.
  // Args:
  //    |mode|: The index of the mode from the supported modes.
  zx_status_t CameraSensorSetMode(uint8_t mode);

  // Causes the sensor to start streaming.
  zx_status_t CameraSensorStartStreaming();

  // Causes the sensor to stop streaming.
  zx_status_t CameraSensorStopStreaming();

  // Sets the analog gain of the sensor.
  // Args:
  //    |gain|: The amount of analog gain to set in the sensor.
  // Returns:
  //    The same value that is passed in.
  int32_t CameraSensorSetAnalogGain(int32_t gain);

  // Sets the digital gain of the sensor.
  // Args:
  //    |gain|: The amount of digital gain to set in the sensor.
  // Returns:
  //    The same value that is passed in.
  int32_t CameraSensorSetDigitalGain(int32_t gain);

  // Sets the integration time for the sensor.
  // Args:
  //    |integration_time|: The value to set the integration time to.
  zx_status_t CameraSensorSetIntegrationTime(int32_t integration_time);

  // Updates the sensor.
  zx_status_t CameraSensorUpdate() { return ZX_OK; }

  // Gets the information about the sensor and stores it in a sensor_info_t.
  // Args:
  //    |out_info|: Pointer to a sensor_info_t where the sensors info will go.
  zx_status_t CameraSensorGetInfo(sensor_info_t* out_info);

  // Gets the supported modes of the sensor.
  // Args:
  //    |out_modes_list|: Pointer to sensor_mode_t where list of supported modes
  //        will be stored.
  //    |modes_count|:
  //    |out_modes_actual|:
  zx_status_t CameraSensorGetSupportedModes(sensor_mode_t* out_modes_list,
                                            size_t modes_count,
                                            size_t* out_modes_actual);

  // Helper methods for test.

  // Checks if the sensor has been initialized.
  // Returns:
  //     A boolean of whether the sensor is initialized or not.
  bool IsInitialized() { return is_initialized_; }

  // Gets the mode that the sensor is currently set to.
  // Returns:
  //     A sensor_mode_t that the sensor is currently set to.
  sensor_mode_t GetMode() { return supported_modes_[mode_]; }

  // Checks if the sensor is streaming.
  bool IsStreaming() { return is_streaming_; }

  // Gets the value the analog gain of the sensor is set to.
  int32_t GetAnalogGain() { return analog_gain_; }

  // Gets the value the digital gain of the sensor is set to.
  int32_t GetDigitalGain() { return digital_gain_; }

  // Get the value the integration time of the sensor is set to.
  int32_t GetIntegrationTime() { return integration_time_; }

  // Sets the sensor info.
  // Args:
  //    |info|: A sensor_info_t with info values to be set.
  zx_status_t SetSensorInfo(const sensor_info_t& info);

  // Adds a mode to the supported modes.
  // Args:
  //    |mode|: The mode to add to the supported modes.
  zx_status_t AddMode(const sensor_mode_t& mode);

 private:
  bool is_initialized_ = false;
  bool is_streaming_ = false;

  uint8_t mode_ = 0;

  int32_t analog_gain_ = 10;
  int32_t digital_gain_ = 100;
  int32_t integration_time_ = 100;

  std::vector<sensor_mode_t> supported_modes_ = {
      {
          .fpms = 30000,
          .resolution =
              {
                  .width = 1920,
                  .height = 1080,
              },
          .exposures = 1,
          .wdr_mode = WDR_MODE_LINEAR,
          .bits = 10,
          .lanes = 2,
          .mbps = 1000,
          .idx = 0,
          .bayer = BAYER_RGGB,
      },
  };
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_SENSORS_MOCK_SENSOR_MOCK_SENSOR_DEVICE_H_
