// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpio-test.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <memory>

#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>

#include "src/devices/gpio/drivers/gpio-test/gpio_test_bind.h"

namespace gpio_test {

void GpioTest::DdkRelease() {
  done_ = true;
  thrd_join(output_thread_, nullptr);
  thrd_join(interrupt_thread_, nullptr);
  gpios_[GPIO_BUTTON].ReleaseInterrupt();

  delete this;
}

// test thread that cycles all of the GPIOs provided to us
int GpioTest::OutputThread() {
  for (uint32_t i = 0; i < gpio_count_ - 1; i++) {
    if (gpios_[i].ConfigOut(0) != ZX_OK) {
      zxlogf(ERROR, "gpio-test: ConfigOut failed for gpio %u", i);
      return -1;
    }
  }

  while (!done_) {
    // Assuming here that the last GPIO is the input button
    // so we don't toggle that one
    for (uint32_t i = 0; i < gpio_count_ - 1; i++) {
      gpios_[i].Write(1);
      sleep(1);
      gpios_[i].Write(0);
      sleep(1);
    }
  }

  return 0;
}

// test thread that cycles runs tests for GPIO interrupts
int GpioTest::InterruptThread() {
  if (gpios_[GPIO_BUTTON].ConfigIn(GPIO_PULL_DOWN) != ZX_OK) {
    zxlogf(ERROR, "%s: gpio_config failed for gpio %u ", __func__, GPIO_BUTTON);
    return -1;
  }

  if (gpios_[GPIO_BUTTON].GetInterrupt(ZX_INTERRUPT_MODE_EDGE_HIGH, &interrupt_) != ZX_OK) {
    zxlogf(ERROR, "%s: gpio_get_interrupt failed for gpio %u", __func__, GPIO_BUTTON);
    return -1;
  }

  while (!done_) {
    zxlogf(INFO, "Waiting for GPIO Test Input Interrupt");
    auto status = interrupt_.wait(nullptr);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: interrupt wait failed %d", __func__, status);
      return -1;
    }
    zxlogf(INFO, "Received GPIO Test Input Interrupt");
    uint8_t out;
    gpios_[GPIO_LED].Read(&out);
    gpios_[GPIO_LED].Write(!out);
    sleep(1);
  }

  return 0;
}

zx_status_t GpioTest::Create(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;
  auto dev = std::unique_ptr<GpioTest>(new (&ac) GpioTest(parent));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  auto status = dev->Init();
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = dev.release();
  return ZX_OK;
}

zx_status_t GpioTest::Init() {
  gpio_count_ = DdkGetFragmentCount();

  fbl::AllocChecker ac;
  gpios_ = fbl::Array(new (&ac) ddk::GpioProtocolClient[gpio_count_], gpio_count_);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  composite_device_fragment_t fragments[gpio_count_];
  size_t actual;
  DdkGetFragments(fragments, gpio_count_, &actual);
  if (actual != gpio_count_) {
    return ZX_ERR_INTERNAL;
  }

  for (uint32_t i = 0; i < gpio_count_; i++) {
    auto status = device_get_protocol(fragments[i].device, ZX_PROTOCOL_GPIO, &gpios_[i]);
    if (status != ZX_OK) {
      return status;
    }
  }

  auto status = DdkAdd("gpio-test", DEVICE_ADD_NON_BINDABLE);
  if (status != ZX_OK) {
    return status;
  }

  thrd_create_with_name(
      &output_thread_,
      [](void* arg) -> int { return reinterpret_cast<GpioTest*>(arg)->OutputThread(); }, this,
      "gpio-test output");
  thrd_create_with_name(
      &interrupt_thread_,
      [](void* arg) -> int { return reinterpret_cast<GpioTest*>(arg)->InterruptThread(); }, this,
      "gpio-test interrupt");

  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = GpioTest::Create;
  return ops;
}();

}  // namespace gpio_test

ZIRCON_DRIVER(gpio_test, gpio_test::driver_ops, "zircon", "0.1");
