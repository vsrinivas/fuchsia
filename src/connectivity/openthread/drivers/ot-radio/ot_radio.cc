// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ot_radio.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/driver-unit-test/utils.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <zircon/compiler.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
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
  PORT_KEY_EXIT_THREAD,
};

OtRadioDevice::OtRadioDevice(zx_device_t* device)
    : ddk::Device<OtRadioDevice, ddk::UnbindableNew>(device),
      loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

zx_status_t OtRadioDevice::StartLoopThread() {
  zxlogf(TRACE, "Start loop thread\n");
  return loop_.StartThread("ot-stack-loop");
}

bool OtRadioDevice::RunUnitTests(void* ctx, zx_device_t* parent, zx_handle_t channel) {
  return driver_unit_test::RunZxTests("OtRadioTests", parent, channel);
}

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

  spi_ = ddk::SpiProtocolClient(components[COMPONENT_SPI]);
  if (!spi_.is_valid()) {
    zxlogf(ERROR, "ot-radio %s: failed to acquire spi\n", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  status = device_get_protocol(components[COMPONENT_INT_GPIO], ZX_PROTOCOL_GPIO,
                               &gpio_[OT_RADIO_INT_PIN]);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio %s: failed to acquire interrupt gpio\n", __func__);
    return status;
  }

  status = gpio_[OT_RADIO_INT_PIN].ConfigIn(GPIO_NO_PULL);

  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio %s: failed to configure interrupt gpio\n", __func__);
    return status;
  }

  status = gpio_[OT_RADIO_INT_PIN].GetInterrupt(ZX_INTERRUPT_MODE_EDGE_LOW, &interrupt_);

  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio %s: failed to get interrupt\n", __func__);
    return status;
  }

  status = device_get_protocol(components[COMPONENT_RESET_GPIO], ZX_PROTOCOL_GPIO,
                               &gpio_[OT_RADIO_RESET_PIN]);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio %s: failed to acquire reset gpio\n", __func__);
    return status;
  }

  status = gpio_[OT_RADIO_RESET_PIN].ConfigOut(1);

  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio %s: failed to configure rst gpio, status = %d", __func__, status);
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

zx_status_t OtRadioDevice::Reset() {
  zx_status_t status = ZX_OK;
  zxlogf(TRACE, "#s: reset\n");

  status = gpio_[OT_RADIO_RESET_PIN].Write(0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: gpio write failed\n");
    return status;
  }
  zx::nanosleep(zx::deadline_after(zx::msec(100)));

  status = gpio_[OT_RADIO_RESET_PIN].Write(1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: gpio write failed\n");
    return status;
  }
  zx::nanosleep(zx::deadline_after(zx::msec(50)));
  return status;
}

zx_status_t OtRadioDevice::RadioThread(void) {
  zx_status_t status = ZX_OK;
  zxlogf(ERROR, "ot-radio: entered thread\n");

  while (true) {
    zx_port_packet_t packet = {};
    auto status = port_.wait(zx::time::infinite(), &packet);

    if (status == ZX_ERR_TIMED_OUT) {
      continue;
    } else if (status != ZX_OK) {
      zxlogf(ERROR, "ot-radio: port wait failed: %d\n", status);
      return thrd_error;
    }

    if (packet.key == PORT_KEY_EXIT_THREAD) {
      break;
    } else if (packet.key == PORT_KEY_RADIO_IRQ) {
      interrupt_.ack();
      zxlogf(TRACE, "ot-radio: interrupt\n");
      // Read packet
      uint8_t i;
      size_t rx_actual;
      size_t read_length = 10;
      spi_.Receive(read_length, &spi_rx_buffer_[0], read_length, &rx_actual);
      // Printing to cross check with bytes seen with a scope
      zxlogf(TRACE, "ot-radio: rx_actual %lu\n", rx_actual);
      for (i = 0; i < read_length; i++) {
        zxlogf(TRACE, "ot-radio: RX %2X\n", spi_rx_buffer_[i]);
      }
      sync_completion_signal(&spi_rx_complete);
    }
  }
  zxlogf(TRACE, "ot-radio: exiting\n");

  return status;
}

zx_status_t OtRadioDevice::CreateBindAndStart(void* ctx, zx_device_t* parent) {
  std::unique_ptr<OtRadioDevice> ot_radio_dev;
  zx_status_t status = Create(ctx, parent, &ot_radio_dev);
  if (status != ZX_OK) {
    return status;
  }

  status = ot_radio_dev->Bind();
  if (status != ZX_OK) {
    return status;
  }
  // device intentionally leaked as it is now held by DevMgr
  auto dev_ptr = ot_radio_dev.release();

  status = dev_ptr->Start();
  if (status != ZX_OK) {
    return status;
  }

  return status;
}

zx_status_t OtRadioDevice::Create(void* ctx, zx_device_t* parent,
                                  std::unique_ptr<OtRadioDevice>* out) {
  auto dev = std::make_unique<OtRadioDevice>(parent);
  zx_status_t status = dev->Init();

  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: Driver init failed %d\n", status);
    return status;
  }

  *out = std::move(dev);

  return ZX_OK;
}

zx_status_t OtRadioDevice::Bind(void) {
  zx_status_t status = DdkAdd("ot-radio");
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: Could not create device: %d\n", status);
    return status;
  } else {
    zxlogf(TRACE, "ot-radio: Added device\n");
  }
  return status;
}

zx_status_t OtRadioDevice::Start(void) {
  zx_status_t status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: port create failed %d\n", status);
    return status;
  }

  status = interrupt_.bind(port_, PORT_KEY_RADIO_IRQ, 0);

  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: interrupt bind failed %d\n", status);
    return status;
  }

  auto cleanup = fbl::MakeAutoCall([&]() { ShutDown(); });

  auto callback = [](void* cookie) {
    return reinterpret_cast<OtRadioDevice*>(cookie)->RadioThread();
  };
  int ret = thrd_create_with_name(&thread_, callback, this, "ot-radio-thread");

  ZX_DEBUG_ASSERT(ret == thrd_success);

  status = StartLoopThread();
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: Could not start loop thread\n");
    return status;
  }

  zxlogf(TRACE, "ot-radio: Started thread\n");

  cleanup.cancel();

  return status;
}

void OtRadioDevice::DdkRelease() { delete this; }

void OtRadioDevice::DdkUnbindNew(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}

zx_status_t OtRadioDevice::ShutDown() {
  zx_port_packet packet = {PORT_KEY_EXIT_THREAD, ZX_PKT_TYPE_USER, ZX_OK, {}};
  port_.queue(&packet);
  thrd_join(thread_, NULL);
  gpio_[OT_RADIO_INT_PIN].ReleaseInterrupt();
  interrupt_.destroy();
  loop_.Shutdown();
  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = OtRadioDevice::CreateBindAndStart;
  ops.run_unit_tests = OtRadioDevice::RunUnitTests;
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
