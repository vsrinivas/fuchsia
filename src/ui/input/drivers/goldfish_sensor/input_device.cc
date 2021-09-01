// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/drivers/goldfish_sensor/input_device.h"

#include <fuchsia/input/report/llcpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/ddk/debug.h>
#include <lib/fit/function.h>
#include <lib/zx/clock.h>

#include <cmath>
#include <cstdint>

#include "lib/fidl/llcpp/arena.h"
#include "src/ui/input/drivers/goldfish_sensor/parser.h"
#include "src/ui/input/drivers/goldfish_sensor/root_device.h"

namespace fir_fidl = ::fuchsia_input_report::wire;

namespace goldfish::sensor {

InputDevice::InputDevice(zx_device_t* parent, async_dispatcher_t* dispatcher,
                         OnDestroyCallback on_destroy)
    : InputDeviceType(parent), dispatcher_(dispatcher), on_destroy_(std::move(on_destroy)) {}

InputDevice::~InputDevice() {
  if (on_destroy_) {
    on_destroy_(this);
  }
}

void AccelerationInputDevice::InputReport::ToFidlInputReport(fir_fidl::InputReport& input_report,
                                                             fidl::AnyArena& allocator) {
  fir_fidl::SensorInputReport sensor_report(allocator);
  fidl::VectorView<int64_t> values(allocator, 3);

  // Sensor reading uses unit |m/s^2|, while InputReport uses unit
  // |m/s^2 * 1e-2| and only accepts integer.
  // This converts the sensor reading to InputReport values.
  constexpr auto SensorReadingToFidlReportValue = [](float reading) -> int {
    return static_cast<int>(reading * 100);
  };
  values[0] = SensorReadingToFidlReportValue(x);
  values[1] = SensorReadingToFidlReportValue(y);
  values[2] = SensorReadingToFidlReportValue(z);
  sensor_report.set_values(allocator, std::move(values));

  input_report.set_event_time(allocator, event_time.get());
  input_report.set_sensor(allocator, std::move(sensor_report));
}

fit::result<InputDevice*, zx_status_t> AccelerationInputDevice::Create(
    RootDevice* sensor, async_dispatcher_t* dispatcher) {
  // Parent device (sensor) is guaranteed to outlive the child input device
  // (dev), so it's safe to use raw pointers here.
  auto device = std::make_unique<AccelerationInputDevice>(
      sensor->zxdev(), dispatcher,
      [sensor](InputDevice* dev) { sensor->input_devices()->RemoveDevice(dev); });
  zx_status_t status;

  if ((status = device->DdkAdd("goldfish-sensor-accel")) != ZX_OK) {
    return fit::error(status);
  }

  // Device will be owned by devmgr.
  return fit::ok(device.release());
}

zx_status_t AccelerationInputDevice::OnReport(const SensorReport& rpt) {
  InputReport input_report;
  input_report.event_time = zx::clock::get_monotonic();

  if (rpt.data.size() != 3) {
    zxlogf(ERROR, "AccelerationInputDevice: invalid data size: %lu", rpt.data.size());
    return ZX_ERR_INVALID_ARGS;
  }

  if (auto p = std::get_if<Numeric>(&rpt.data[0]); p != nullptr) {
    input_report.x = static_cast<float>(p->Double());
  } else {
    zxlogf(ERROR, "AccelerationInputDevice: invalid x");
    return ZX_ERR_INVALID_ARGS;
  }

  if (auto p = std::get_if<Numeric>(&rpt.data[1]); p != nullptr) {
    input_report.y = static_cast<float>(p->Double());
  } else {
    zxlogf(ERROR, "AccelerationInputDevice: invalid y");
    return ZX_ERR_INVALID_ARGS;
  }

  if (auto p = std::get_if<Numeric>(&rpt.data[2]); p != nullptr) {
    input_report.z = static_cast<float>(p->Double());
  } else {
    zxlogf(ERROR, "AccelerationInputDevice: invalid z");
    return ZX_ERR_INVALID_ARGS;
  }

  input_report_readers_.SendReportToAllReaders(input_report);
  return ZX_OK;
}

void AccelerationInputDevice::GetDescriptor(GetDescriptorRequestView request,
                                            GetDescriptorCompleter::Sync& completer) {
  constexpr size_t kDescriptorBufferSize = 512;

  constexpr fir_fidl::Axis kAxis = {
      .range = {.min = INT64_MIN, .max = INT64_MAX},
      // unit: 0.01 m/s^2
      .unit = {.type = fir_fidl::UnitType::kSiLinearAcceleration, .exponent = -2},
  };

  fidl::Arena<kDescriptorBufferSize> allocator;

  fir_fidl::DeviceInfo device_info;
  device_info.vendor_id = static_cast<uint32_t>(fir_fidl::VendorId::kGoogle);
  device_info.product_id =
      static_cast<uint32_t>(fir_fidl::VendorGoogleProductId::kGoldfishAccelerationSensor);

  fidl::VectorView<fir_fidl::SensorAxis> sensor_axes(allocator, 3);
  sensor_axes[0].axis = kAxis;
  sensor_axes[1].axis = kAxis;
  sensor_axes[2].axis = kAxis;
  sensor_axes[0].type = fir_fidl::SensorType::kAccelerometerX;
  sensor_axes[1].type = fir_fidl::SensorType::kAccelerometerY;
  sensor_axes[2].type = fir_fidl::SensorType::kAccelerometerZ;

  fir_fidl::SensorInputDescriptor sensor_input_descriptor(allocator);
  sensor_input_descriptor.set_values(allocator, std::move(sensor_axes));

  fir_fidl::SensorDescriptor sensor_descriptor(allocator);
  sensor_descriptor.set_input(allocator, std::move(sensor_input_descriptor));

  fir_fidl::DeviceDescriptor descriptor(allocator);
  descriptor.set_device_info(allocator, std::move(device_info));
  descriptor.set_sensor(allocator, std::move(sensor_descriptor));

  completer.Reply(std::move(descriptor));
}

void AccelerationInputDevice::GetInputReportsReader(
    GetInputReportsReaderRequestView request, GetInputReportsReaderCompleter::Sync& completer) {
  zx_status_t status = input_report_readers_.CreateReader(dispatcher(), std::move(request->reader));
  ZX_DEBUG_ASSERT(status == ZX_OK);
}

void GyroscopeInputDevice::InputReport::ToFidlInputReport(fir_fidl::InputReport& input_report,
                                                          fidl::AnyArena& allocator) {
  fir_fidl::SensorInputReport sensor_report(allocator);
  fidl::VectorView<int64_t> values(allocator, 3);
  // Raw sensor reading uses unit |rad/s|, while InputReport uses unit
  // |deg/s * 1e-2| and only accepts integer.
  // This converts the sensor reading to InputReport values.
  constexpr auto SensorReadingToFidlReportValue = [](float reading) -> int {
    return static_cast<int>(reading * 180 / M_PI * 100);
  };
  values[0] = SensorReadingToFidlReportValue(x);
  values[1] = SensorReadingToFidlReportValue(y);
  values[2] = SensorReadingToFidlReportValue(z);
  sensor_report.set_values(allocator, std::move(values));

  input_report.set_event_time(allocator, event_time.get());
  input_report.set_sensor(allocator, std::move(sensor_report));
}

fit::result<InputDevice*, zx_status_t> GyroscopeInputDevice::Create(
    RootDevice* sensor, async_dispatcher_t* dispatcher) {
  // Parent device (sensor) is guaranteed to outlive the child input device
  // (dev), so it's safe to use raw pointers here.
  auto device = std::make_unique<GyroscopeInputDevice>(
      sensor->zxdev(), dispatcher,
      [sensor](InputDevice* dev) { sensor->input_devices()->RemoveDevice(dev); });
  zx_status_t status;

  if ((status = device->DdkAdd("goldfish-sensor-gyroscope")) != ZX_OK) {
    return fit::error(status);
  }

  // Device will be owned by devmgr.
  return fit::ok(device.release());
}

zx_status_t GyroscopeInputDevice::OnReport(const SensorReport& rpt) {
  InputReport input_report;
  input_report.event_time = zx::clock::get_monotonic();

  if (rpt.data.size() != 3) {
    zxlogf(ERROR, "GyroscopeInputDevice: invalid data size: %lu", rpt.data.size());
    return ZX_ERR_INVALID_ARGS;
  }

  if (auto p = std::get_if<Numeric>(&rpt.data[0]); p != nullptr) {
    input_report.x = static_cast<float>(p->Double());
  } else {
    zxlogf(ERROR, "GyroscopeInputDevice: invalid x");
    return ZX_ERR_INVALID_ARGS;
  }

  if (auto p = std::get_if<Numeric>(&rpt.data[1]); p != nullptr) {
    input_report.y = static_cast<float>(p->Double());
  } else {
    zxlogf(ERROR, "GyroscopeInputDevice: invalid y");
    return ZX_ERR_INVALID_ARGS;
  }

  if (auto p = std::get_if<Numeric>(&rpt.data[2]); p != nullptr) {
    input_report.z = static_cast<float>(p->Double());
  } else {
    zxlogf(ERROR, "GyroscopeInputDevice: invalid z");
    return ZX_ERR_INVALID_ARGS;
  }

  input_report_readers_.SendReportToAllReaders(input_report);
  return ZX_OK;
}

void GyroscopeInputDevice::GetDescriptor(GetDescriptorRequestView request,
                                         GetDescriptorCompleter::Sync& completer) {
  constexpr size_t kDescriptorBufferSize = 512;

  constexpr fir_fidl::Axis kAxis = {
      .range = {.min = INT64_MIN, .max = INT64_MAX},
      // unit: 0.01 deg/s
      .unit = {.type = fir_fidl::UnitType::kEnglishAngularVelocity, .exponent = -2},
  };

  fidl::Arena<kDescriptorBufferSize> allocator;

  fir_fidl::DeviceInfo device_info;
  device_info.vendor_id = static_cast<uint32_t>(fir_fidl::VendorId::kGoogle);
  device_info.product_id =
      static_cast<uint32_t>(fir_fidl::VendorGoogleProductId::kGoldfishGyroscopeSensor);

  fidl::VectorView<fir_fidl::SensorAxis> sensor_axes(allocator, 3);
  sensor_axes[0].axis = kAxis;
  sensor_axes[1].axis = kAxis;
  sensor_axes[2].axis = kAxis;
  sensor_axes[0].type = fir_fidl::SensorType::kGyroscopeX;
  sensor_axes[1].type = fir_fidl::SensorType::kGyroscopeY;
  sensor_axes[2].type = fir_fidl::SensorType::kGyroscopeZ;

  fir_fidl::SensorInputDescriptor sensor_input_descriptor(allocator);
  sensor_input_descriptor.set_values(allocator, std::move(sensor_axes));

  fir_fidl::SensorDescriptor sensor_descriptor(allocator);
  sensor_descriptor.set_input(allocator, std::move(sensor_input_descriptor));

  fir_fidl::DeviceDescriptor descriptor(allocator);
  descriptor.set_device_info(allocator, std::move(device_info));
  descriptor.set_sensor(allocator, std::move(sensor_descriptor));

  completer.Reply(std::move(descriptor));
}

void GyroscopeInputDevice::GetInputReportsReader(GetInputReportsReaderRequestView request,
                                                 GetInputReportsReaderCompleter::Sync& completer) {
  zx_status_t status = input_report_readers_.CreateReader(dispatcher(), std::move(request->reader));
  ZX_DEBUG_ASSERT(status == ZX_OK);
}

void RgbcLightInputDevice::InputReport::ToFidlInputReport(fir_fidl::InputReport& input_report,
                                                          fidl::AnyArena& allocator) {
  fir_fidl::SensorInputReport sensor_report(allocator);
  fidl::VectorView<int64_t> values(allocator, 4);
  values[0] = static_cast<int64_t>(r);
  values[1] = static_cast<int64_t>(g);
  values[2] = static_cast<int64_t>(b);
  values[3] = static_cast<int64_t>(c);
  sensor_report.set_values(allocator, std::move(values));

  input_report.set_event_time(allocator, event_time.get());
  input_report.set_sensor(allocator, std::move(sensor_report));
}

fit::result<InputDevice*, zx_status_t> RgbcLightInputDevice::Create(
    RootDevice* sensor, async_dispatcher_t* dispatcher) {
  // Parent device (sensor) is guaranteed to outlive the child input device
  // (dev), so it's safe to use raw pointers here.
  auto device = std::make_unique<RgbcLightInputDevice>(
      sensor->zxdev(), dispatcher,
      [sensor](InputDevice* dev) { sensor->input_devices()->RemoveDevice(dev); });
  zx_status_t status;

  if ((status = device->DdkAdd("goldfish-sensor-rgbclight")) != ZX_OK) {
    return fit::error(status);
  }

  // Device will be owned by devmgr.
  return fit::ok(device.release());
}

zx_status_t RgbcLightInputDevice::OnReport(const SensorReport& rpt) {
  InputReport input_report;
  input_report.event_time = zx::clock::get_monotonic();

  if (rpt.data.size() != 4) {
    zxlogf(ERROR, "RgbcLightInputDevice: invalid data size: %lu", rpt.data.size());
    return ZX_ERR_INVALID_ARGS;
  }

  if (auto p = std::get_if<Numeric>(&rpt.data[0]); p != nullptr) {
    input_report.r = static_cast<float>(p->Double());
  } else {
    zxlogf(ERROR, "RgbcLightInputDevice: invalid r");
    return ZX_ERR_INVALID_ARGS;
  }

  if (auto p = std::get_if<Numeric>(&rpt.data[1]); p != nullptr) {
    input_report.g = static_cast<float>(p->Double());
  } else {
    zxlogf(ERROR, "RgbcLightInputDevice: invalid g");
    return ZX_ERR_INVALID_ARGS;
  }

  if (auto p = std::get_if<Numeric>(&rpt.data[2]); p != nullptr) {
    input_report.b = static_cast<float>(p->Double());
  } else {
    zxlogf(ERROR, "RgbcLightInputDevice: invalid b");
    return ZX_ERR_INVALID_ARGS;
  }

  if (auto p = std::get_if<Numeric>(&rpt.data[3]); p != nullptr) {
    input_report.c = static_cast<float>(p->Double());
  } else {
    zxlogf(ERROR, "RgbcLightInputDevice: invalid c");
    return ZX_ERR_INVALID_ARGS;
  }

  input_report_readers_.SendReportToAllReaders(input_report);
  return ZX_OK;
}

void RgbcLightInputDevice::GetDescriptor(GetDescriptorRequestView request,
                                         GetDescriptorCompleter::Sync& completer) {
  constexpr size_t kDescriptorBufferSize = 512;

  constexpr fir_fidl::Axis kAxis = {
      .range = {.min = 0, .max = UINT16_MAX},
      .unit = {.type = fir_fidl::UnitType::kNone},
  };

  fidl::Arena<kDescriptorBufferSize> allocator;

  fir_fidl::DeviceInfo device_info;
  device_info.vendor_id = static_cast<uint32_t>(fir_fidl::VendorId::kGoogle);
  device_info.product_id =
      static_cast<uint32_t>(fir_fidl::VendorGoogleProductId::kGoldfishRgbcLightSensor);

  fidl::VectorView<fir_fidl::SensorAxis> sensor_axes(allocator, 4);
  sensor_axes[0].axis = kAxis;
  sensor_axes[1].axis = kAxis;
  sensor_axes[2].axis = kAxis;
  sensor_axes[3].axis = kAxis;
  sensor_axes[0].type = fir_fidl::SensorType::kLightRed;
  sensor_axes[1].type = fir_fidl::SensorType::kLightGreen;
  sensor_axes[2].type = fir_fidl::SensorType::kLightBlue;
  sensor_axes[3].type = fir_fidl::SensorType::kLightIlluminance;

  fir_fidl::SensorInputDescriptor sensor_input_descriptor(allocator);
  sensor_input_descriptor.set_values(allocator, std::move(sensor_axes));

  fir_fidl::SensorDescriptor sensor_descriptor(allocator);
  sensor_descriptor.set_input(allocator, std::move(sensor_input_descriptor));

  fir_fidl::DeviceDescriptor descriptor(allocator);
  descriptor.set_device_info(allocator, std::move(device_info));
  descriptor.set_sensor(allocator, std::move(sensor_descriptor));

  completer.Reply(std::move(descriptor));
}

void RgbcLightInputDevice::GetInputReportsReader(GetInputReportsReaderRequestView request,
                                                 GetInputReportsReaderCompleter::Sync& completer) {
  zx_status_t status = input_report_readers_.CreateReader(dispatcher(), std::move(request->reader));
  ZX_DEBUG_ASSERT(status == ZX_OK);
}

}  // namespace goldfish::sensor
