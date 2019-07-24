// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_HW_ACCEL_GDC_TEST_GDC_ON_DEVICE_TEST_H_
#define SRC_CAMERA_DRIVERS_HW_ACCEL_GDC_TEST_GDC_ON_DEVICE_TEST_H_

#include <memory>

#include <zxtest/zxtest.h>

namespace gdc {
// |GdcDeviceTester| is spawned by the driver in |gdc.cc|
class GdcDevice;

class GdcDeviceTester : public zxtest::Test {
 public:
  static zx_status_t RunTests(GdcDevice* device);

 protected:
  // Setup & TearDown
  void SetUp() override;
  void TearDown() override;
};

}  // namespace gdc

#endif  // SRC_CAMERA_DRIVERS_HW_ACCEL_GDC_TEST_GDC_ON_DEVICE_TEST_H_
