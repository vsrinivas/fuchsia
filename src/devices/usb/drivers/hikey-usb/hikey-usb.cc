// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hikey-usb.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio.h>
#include <ddktl/protocol/composite.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

namespace hikey_usb {

zx_status_t HikeyUsb::Create(zx_device_t* parent) {
  fbl::AllocChecker ac;
  auto bus = fbl::make_unique_checked<HikeyUsb>(&ac, parent);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  auto status = bus->Init();
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = bus.release();
  return ZX_OK;
}

zx_status_t HikeyUsb::Init() {
  ddk::CompositeProtocolClient composite(parent_);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "HikeyUsb::Could not get composite protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_device_t* fragments[FRAGMENT_COUNT];
  size_t actual;
  composite.GetFragments(fragments, fbl::count_of(fragments), &actual);
  if (actual != fbl::count_of(fragments)) {
    zxlogf(ERROR, "HikeyUsb::Could not get fragments");
    return ZX_ERR_NOT_SUPPORTED;
  }

  for (uint32_t i = 0; i < countof(gpios_); i++) {
    // fragments[0] is platform device, which is only used for providing metadata.
    auto status = device_get_protocol(fragments[i + 1], ZX_PROTOCOL_GPIO, &gpios_[i]);
    if (status != ZX_OK) {
      return status;
    }
    gpio_config_out(&gpios_[i], 0);
  }

  zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GENERIC},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_USB_DWC3},
  };

  return DdkAdd("dwc3", 0, props, countof(props));
}

zx_status_t HikeyUsb::UsbModeSwitchSetMode(usb_mode_t mode) {
  if (mode == usb_mode_) {
    return ZX_OK;
  }
  if (mode == USB_MODE_OTG) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  gpio_write(&gpios_[HUB_VDD33_EN], mode == USB_MODE_HOST);
  gpio_write(&gpios_[VBUS_TYPEC], mode == USB_MODE_HOST);
  gpio_write(&gpios_[USBSW_SW_SEL], mode == USB_MODE_HOST);

  usb_mode_ = mode;
  return ZX_OK;

  return ZX_OK;
}

void HikeyUsb::DdkRelease() { delete this; }

__BEGIN_CDECLS
static zx_status_t hikey_usb_bind(void* ctx, zx_device_t* parent) {
  return hikey_usb::HikeyUsb::Create(parent);
}
__END_CDECLS

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = hikey_usb_bind;
  return ops;
}();

}  // namespace hikey_usb

ZIRCON_DRIVER_BEGIN(hikey_usb, hikey_usb::driver_ops, "zircon", "0.1", 4)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_96BOARDS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_HIKEY960),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_HIKEY960_USB), ZIRCON_DRIVER_END(hikey_usb)
