// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BUS_DRIVERS_PLATFORM_TEST_TEST_H_
#define SRC_DEVICES_BUS_DRIVERS_PLATFORM_TEST_TEST_H_

#include <threads.h>

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/platform/bus.h>
#include <fbl/macros.h>

namespace board_test {

class TestBoard;
using TestBoardType = ddk::Device<TestBoard>;

// This is the main class for the platform bus driver.
class TestBoard : public TestBoardType {
 public:
  explicit TestBoard(zx_device_t* parent, pbus_protocol_t* pbus)
      : TestBoardType(parent), pbus_(pbus) {}

  static zx_status_t Create(zx_device_t* parent);

  // Device protocol implementation.
  void DdkRelease();

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(TestBoard);

  zx_status_t Start();
  zx_status_t GpioInit();
  zx_status_t I2cInit();
  zx_status_t SpiInit();
  zx_status_t PowerInit();
  zx_status_t ClockInit();
  zx_status_t AudioCodecInit();
  zx_status_t PwmInit();
  zx_status_t RpmbInit();
  zx_status_t TestInit();
  int Thread();

  ddk::PBusProtocolClient pbus_;
  thrd_t thread_;
};

}  // namespace board_test

__BEGIN_CDECLS
zx_status_t test_bind(void* ctx, zx_device_t* parent);
__END_CDECLS

#endif  // SRC_DEVICES_BUS_DRIVERS_PLATFORM_TEST_TEST_H_
