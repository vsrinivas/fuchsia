// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ot_radio.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <zircon/compiler.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <hw/arch_ops.h>
#include <hw/reg.h>


namespace ot_radio {

enum {
  COMPONENT_PDEV,
  COMPONENT_SPI,
  COMPONENT_INT_GPIO,
  COMPONENT_RESET_GPIO,
  COMPONENT_COUNT,
};

enum {
  PORT_KEY_RADIO_IRQ,
  PORT_KEY_TX_TO_APP,
  PORT_KEY_RX_FROM_APP,
};

OtRadioDevice::OtRadioDevice(zx_device_t* device)
    : ddk::Device<OtRadioDevice, ddk::Unbindable>(device) {}

zx_status_t OtRadioDevice::Init() {
  composite_protocol_t composite;

  auto status = device_get_protocol(parent(), ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not get composite protocol\n");
    return status;
  }

  zx_device_t* components[COMPONENT_COUNT];
  size_t actual;
  composite_get_components(&composite, components, fbl::count_of(components), &actual);
  if (actual != fbl::count_of(components)) {
    zxlogf(ERROR, "could not get components\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  status = device_get_protocol(components[COMPONENT_INT_GPIO], ZX_PROTOCOL_GPIO,
                               &gpio_[OT_RADIO_INT_PIN]);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: failed to acquire gpio\n");
    return status;
  }

  gpio_config_in(&gpio_[OT_RADIO_INT_PIN], GPIO_NO_PULL);

  status = gpio_get_interrupt(&gpio_[OT_RADIO_INT_PIN], ZX_INTERRUPT_MODE_EDGE_LOW,
                              interrupt_.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  status = device_get_protocol(components[COMPONENT_RESET_GPIO], ZX_PROTOCOL_GPIO,
                               &gpio_[OT_RADIO_RESET_PIN]);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: failed to acquire gpio\n");
    return status;
  }

  status = gpio_config_out(&gpio_[OT_RADIO_RESET_PIN], 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: failed to configure rst gpio, status = %d", status);
    return status;
  }

  uint32_t device_id;
  status = device_get_metadata(components[COMPONENT_PDEV], DEVICE_METADATA_PRIVATE, &device_id,
                               sizeof(device_id), &actual);
  if (status != ZX_OK || sizeof(device_id) != actual) {
    zxlogf(ERROR, "ot-radio: failed to read metadata\n");
    return status == ZX_OK ? ZX_ERR_INTERNAL : status;
  }

  return ZX_OK;
}

void OtRadioDevice::Reset() {
  zxlogf(TRACE, "ot-radio: reset\n");
  gpio_write(&gpio_[OT_RADIO_RESET_PIN], 0);
  zx::nanosleep(zx::deadline_after(zx::msec(100)));
  gpio_write(&gpio_[OT_RADIO_RESET_PIN], 1);
  zx::nanosleep(zx::deadline_after(zx::msec(50)));
}

int OtRadioDevice::Thread() {
  zx_status_t status = 0;

  zxlogf(TRACE, "ot-radio: entering thread\n");

  Reset();
  while (true) {
    zx_port_packet_t packet = {};
    auto status = port_.wait(zx::time::infinite(), &packet);

    if (status != ZX_OK) {
      zxlogf(ERROR, "%s port wait failed: %d\n", __func__, status);
      return thrd_error;
    }

    if (packet.key == PORT_KEY_RADIO_IRQ) {
      interrupt_.ack();
      zxlogf(TRACE, "ot-radio: interrupt\n");
    }
  }
  zxlogf(TRACE, "ot-radio: exiting\n");

  return status;
}

zx_status_t OtRadioDevice::Create(void* ctx, zx_device_t* device) {
  zxlogf(TRACE, "ot-radio: driver started...\n");

  auto ot_radio_dev = std::make_unique<OtRadioDevice>(device);
  zx_status_t status = ot_radio_dev->Init();

  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: Driver bind failed %d\n", status);
    return status;
  }

  status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &ot_radio_dev->port_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s port create failed %d\n", __func__, status);
    return status;
  }

  status = ot_radio_dev->interrupt_.bind(ot_radio_dev->port_, PORT_KEY_RADIO_IRQ, 0 /*options*/);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s interrupt bind failed %d\n", __func__, status);
    return status;
  }

  auto cleanup = fbl::MakeAutoCall([&]() { ot_radio_dev->ShutDown(); });

  auto cb = [](void* arg) -> int { return reinterpret_cast<OtRadioDevice*>(arg)->Thread(); };

  int ret = thrd_create_with_name(&ot_radio_dev->thread_, cb,
                                  reinterpret_cast<void*>(ot_radio_dev.get()), "ot-radio-thread");
  ZX_DEBUG_ASSERT(ret == thrd_success);

  status = ot_radio_dev->DdkAdd("ot-radio");
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: Could not create device: %d\n", status);
    return status;
  } else {
    zxlogf(TRACE, "ot-radio: Added device\n");
  }

  cleanup.cancel();

  // device intentionally leaked as it is now held by DevMgr
  __UNUSED auto ptr = ot_radio_dev.release();

  return ZX_OK;
}

void OtRadioDevice::DdkRelease() { delete this; }

void OtRadioDevice::DdkUnbind() {
  ShutDown();
  DdkRemove();
}

zx_status_t OtRadioDevice::ShutDown() {
  interrupt_.destroy();
  thrd_join(thread_, NULL);
  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = OtRadioDevice::Create;
  return ops;
}();

}  // namespace ot_radio

// clang-format off
ZIRCON_DRIVER_BEGIN(ot_radio, ot_radio::driver_ops, "ot_radio", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_OT_RADIO),
ZIRCON_DRIVER_END(ot_radio)
    // clang-format on
