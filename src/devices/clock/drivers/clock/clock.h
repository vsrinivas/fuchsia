// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_CLOCK_DRIVERS_CLOCK_CLOCK_H_
#define SRC_DEVICES_CLOCK_DRIVERS_CLOCK_CLOCK_H_

#include <fuchsia/hardware/clock/cpp/banjo.h>
#include <fuchsia/hardware/clockimpl/cpp/banjo.h>
#include <lib/ddk/platform-defs.h>

#include <ddktl/device.h>

class ClockDevice;
using ClockDeviceType = ddk::Device<ClockDevice>;

class ClockDevice : public ClockDeviceType,
                    public ddk::ClockProtocol<ClockDevice, ddk::base_protocol> {
 public:
  ClockDevice(zx_device_t* parent, clock_impl_protocol_t* clock, uint32_t id)
      : ClockDeviceType(parent), clock_(clock), id_(id) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkRelease();

  zx_status_t ClockEnable();
  zx_status_t ClockDisable();
  zx_status_t ClockIsEnabled(bool* out_enabled);

  zx_status_t ClockSetRate(uint64_t hz);
  zx_status_t ClockQuerySupportedRate(uint64_t max_rate, uint64_t* out_max_supported_rate);
  zx_status_t ClockGetRate(uint64_t* out_current_rate);

  zx_status_t ClockSetInput(uint32_t idx);
  zx_status_t ClockGetNumInputs(uint32_t* out);
  zx_status_t ClockGetInput(uint32_t* out);

 private:
  const ddk::ClockImplProtocolClient clock_;
  const uint32_t id_;
};

#endif  // SRC_DEVICES_CLOCK_DRIVERS_CLOCK_CLOCK_H_
