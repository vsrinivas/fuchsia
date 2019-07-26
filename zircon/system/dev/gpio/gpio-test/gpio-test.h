// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/gpio.h>
#include <fbl/array.h>
#include <lib/zx/interrupt.h>

#include <threads.h>

namespace gpio_test {

class GpioTest;
using GpioTestType = ddk::Device<GpioTest>;

class GpioTest : public GpioTestType {
 public:
 public:
  explicit GpioTest(zx_device_t* parent) : GpioTestType(parent) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation.
  void DdkRelease();

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(GpioTest);

  // GPIO indices
  enum {
    GPIO_LED,
    GPIO_BUTTON,
  };

  zx_status_t Init();
  int OutputThread();
  int InterruptThread();

  fbl::Array<ddk::GpioProtocolClient> gpios_;

  uint32_t gpio_count_;
  thrd_t output_thread_;
  thrd_t interrupt_thread_;
  bool done_;
  zx::interrupt interrupt_;
};

}  // namespace gpio_test
