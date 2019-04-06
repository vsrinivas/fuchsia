// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/sensor.h"

#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/units.h>
#include <hid-parser/usages.h>

#include <stdint.h>
#include <stdio.h>
#include <vector>

#include <src/lib/fxl/logging.h>

namespace {

bool IsXUsage(const hid::Usage& u) {
  return (u == hid::USAGE(hid::usage::Page::kSensor,
                          hid::usage::Sensor::kAccelerationAxisX)) ||
         (u == hid::USAGE(hid::usage::Page::kSensor,
                          hid::usage::Sensor::kDistanceAxisX)) ||
         (u == hid::USAGE(hid::usage::Page::kSensor,
                          hid::usage::Sensor::kTiltAxisX)) ||
         (u == hid::USAGE(hid::usage::Page::kSensor,
                          hid::usage::Sensor::kMagneticFluxAxisX));
}

bool IsYUsage(const hid::Usage& u) {
  return (u == hid::USAGE(hid::usage::Page::kSensor,
                          hid::usage::Sensor::kAccelerationAxisY)) ||
         (u == hid::USAGE(hid::usage::Page::kSensor,
                          hid::usage::Sensor::kDistanceAxisY)) ||
         (u == hid::USAGE(hid::usage::Page::kSensor,
                          hid::usage::Sensor::kTiltAxisY)) ||
         (u == hid::USAGE(hid::usage::Page::kSensor,
                          hid::usage::Sensor::kMagneticFluxAxisY));
}

bool IsZUsage(const hid::Usage& u) {
  return (u == hid::USAGE(hid::usage::Page::kSensor,
                          hid::usage::Sensor::kAccelerationAxisZ)) ||
         (u == hid::USAGE(hid::usage::Page::kSensor,
                          hid::usage::Sensor::kDistanceAxisZ)) ||
         (u == hid::USAGE(hid::usage::Page::kSensor,
                          hid::usage::Sensor::kTiltAxisZ)) ||
         (u == hid::USAGE(hid::usage::Page::kSensor,
                          hid::usage::Sensor::kMagneticFluxAxisZ));
}

}  // namespace

namespace mozart {

bool Sensor::ParseReportDescriptor(
    const hid::ReportDescriptor& report_descriptor,
    Descriptor* device_descriptor) {
  FXL_CHECK(device_descriptor);

  fuchsia::ui::input::SensorType sensor_type;
  hid::Attributes x = {};
  hid::Attributes y = {};
  hid::Attributes z = {};
  hid::Attributes scalar = {};
  uint32_t caps = 0;

  auto sensor_usage = report_descriptor.input_fields[0].col->usage;
  if (sensor_usage == hid::USAGE(hid::usage::Page::kSensor,
                                 hid::usage::Sensor::kAccelerometer3D)) {
    sensor_type = fuchsia::ui::input::SensorType::ACCELEROMETER;

  } else if (sensor_usage == hid::USAGE(hid::usage::Page::kSensor,
                                        hid::usage::Sensor::kGyrometer3D)) {
    sensor_type = fuchsia::ui::input::SensorType::GYROSCOPE;

  } else if (sensor_usage == hid::USAGE(hid::usage::Page::kSensor,
                                        hid::usage::Sensor::kMagnetometer)) {
    sensor_type = fuchsia::ui::input::SensorType::MAGNETOMETER;

  } else if (sensor_usage == hid::USAGE(hid::usage::Page::kSensor,
                                        hid::usage::Sensor::kAmbientLight)) {
    sensor_type = fuchsia::ui::input::SensorType::LIGHTMETER;

  } else {
    FXL_LOG(ERROR) << "Sensor report descriptor: Sensor page not supported (0x"
                   << std::hex << sensor_usage.usage << ")";
    return false;
  }

  for (size_t i = 0; i < report_descriptor.input_count; i++) {
    const hid::ReportField& field = report_descriptor.input_fields[i];

    if (IsXUsage(field.attr.usage)) {
      x = field.attr;
      caps |= Capabilities::X;
    } else if (IsYUsage(field.attr.usage)) {
      y = field.attr;
      caps |= Capabilities::Y;
    } else if (IsZUsage(field.attr.usage)) {
      z = field.attr;
      caps |= Capabilities::Z;
    } else {
      // At this point, any non X, Y, Z fields in a sensor are put into
      // scalar. InputReport only supports a single scalar so we will
      // pick the last value we see.
      scalar = field.attr;
      caps |= Capabilities::SCALAR;
    }
  }

  if ((caps & Capabilities::SCALAR) &&
      (caps & (Capabilities::X | Capabilities::Y | Capabilities::Z))) {
    FXL_LOG(ERROR) << "Sensor report descriptor: Sensor describes Axis and "
                      "Scalar, must only describe one";
    return false;
  }

  if (caps == 0) {
    FXL_LOG(ERROR) << "Sensor report descriptor: Sensor has no capabilities";
    return false;
  }

  // TODO(SCN-1312): In order to get min sampling rate, max sampling rate,
  // phys_min, and phys_max, we will need to see example reports of how these
  // things are set. This is currently not supported by the hardcoded sensors
  // either.

  x_ = x;
  y_ = y;
  z_ = z;
  scalar_ = scalar;
  capabilities_ = caps;

  report_size_ = report_descriptor.input_byte_sz;
  report_id_ = report_descriptor.report_id;

  // Set the device descriptor.
  device_descriptor->protocol = Protocol::Sensor;
  device_descriptor->has_sensor = true;
  device_descriptor->sensor_descriptor =
      fuchsia::ui::input::SensorDescriptor::New();
  device_descriptor->sensor_descriptor->type = sensor_type;
  return true;
}

bool Sensor::ParseReport(const uint8_t* data, size_t len,
                         fuchsia::ui::input::InputReport* report) {
  FXL_CHECK(report);
  FXL_CHECK(report->sensor);
  double x = 0;
  double y = 0;
  double z = 0;
  double scalar = 0;

  if (report_size_ != len) {
    FXL_LOG(ERROR) << "Sensor report: Expected size " << report_size_
                   << "Recieved size " << len;
    return false;
  }

  if (capabilities_ & Capabilities::X) {
    if (!hid::ExtractAsUnit(data, len, x_, &x)) {
      FXL_LOG(ERROR) << "Sensor report: Failed to parse X";
      return false;
    }
  }
  if (capabilities_ & Capabilities::Y) {
    if (!hid::ExtractAsUnit(data, len, y_, &y)) {
      FXL_LOG(ERROR) << "Sensor report: Failed to parse Y";
      return false;
    }
  }
  if (capabilities_ & Capabilities::Z) {
    if (!hid::ExtractAsUnit(data, len, z_, &z)) {
      FXL_LOG(ERROR) << "Sensor report: Failed to parse Z";
      return false;
    }
  }
  if (capabilities_ & Capabilities::SCALAR) {
    if (!hid::ExtractAsUnit(data, len, scalar_, &scalar)) {
      FXL_LOG(ERROR) << "Sensor report: Failed to parse Scalar";
      return false;
    }
  }

  FXL_DCHECK(x >= INT16_MIN && x <= INT16_MAX)
      << "X sensor value is truncated.";
  FXL_DCHECK(y >= INT16_MIN && y <= INT16_MAX)
      << "Y sensor value is truncated.";
  FXL_DCHECK(z >= INT16_MIN && z <= INT16_MAX)
      << "Z sensor value is truncated.";

  if (capabilities_ & (Capabilities::X | Capabilities::Y | Capabilities::Z)) {
    std::array<int16_t, 3> axis;
    axis[0] = x;
    axis[1] = y;
    axis[2] = z;
    report->sensor->set_vector(std::move(axis));
  } else if (capabilities_ & Capabilities::SCALAR) {
    report->sensor->set_scalar(scalar);
  }

  return true;
}

}  // namespace mozart
