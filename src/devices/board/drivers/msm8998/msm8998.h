// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_MSM8998_MSM8998_H_
#define SRC_DEVICES_BOARD_DRIVERS_MSM8998_MSM8998_H_

#include <fuchsia/hardware/platform/bus/cpp/banjo.h>
#include <threads.h>

#include <ddktl/device.h>
#include <fbl/macros.h>

namespace board_msm8998 {

class Msm8998;
using Msm8998Type = ddk::Device<Msm8998>;

// This is the main class for the platform bus driver.
class Msm8998 : public Msm8998Type {
 public:
  explicit Msm8998(zx_device_t* parent, pbus_protocol_t* pbus) : Msm8998Type(parent), pbus_(pbus) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation.
  void DdkRelease();

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Msm8998);

  zx_status_t Start();
  int Thread();
  /*
      zx_status_t GpioInit();
      zx_status_t PilInit();
      zx_status_t PowerInit();
      zx_status_t Sdc1Init();
      zx_status_t ClockInit();
  */

  ddk::PBusProtocolClient pbus_;
  thrd_t thread_;
};

}  // namespace board_msm8998

#endif  // SRC_DEVICES_BOARD_DRIVERS_MSM8998_MSM8998_H_
