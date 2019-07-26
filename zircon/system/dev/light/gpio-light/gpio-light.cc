// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpio-light.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddktl/protocol/composite.h>
#include <fbl/alloc_checker.h>

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/assert.h>

namespace gpio_light {

zx_status_t GpioLight::MsgGetName(fidl_txn_t* txn, uint32_t index) {
  if (index >= gpio_count_) {
    return fuchsia_hardware_light_LightGetName_reply(txn, ZX_ERR_OUT_OF_RANGE, nullptr, 0);
  }

  if (names_.size() > 0) {
    auto* name = names_.get() + index * kNameLength;
    return fuchsia_hardware_light_LightGetName_reply(txn, ZX_OK, name, strlen(name) + 1);
  } else {
    // Return "gpio-X" if no metadata was provided.
    char name[20];
    snprintf(name, sizeof(name), "gpio-%u\n", index);
    return fuchsia_hardware_light_LightGetName_reply(txn, ZX_OK, name, strlen(name) + 1);
  }
}

zx_status_t GpioLight::MsgGetCount(fidl_txn_t* txn) {
  return fuchsia_hardware_light_LightGetCount_reply(txn, gpio_count_);
}

zx_status_t GpioLight::MsgHasCapability(uint32_t index,
                                        fuchsia_hardware_light_Capability capability,
                                        fidl_txn_t* txn) {
  if (index >= gpio_count_) {
    return fuchsia_hardware_light_LightHasCapability_reply(txn, ZX_ERR_OUT_OF_RANGE, false);
  }
  return fuchsia_hardware_light_LightHasCapability_reply(txn, ZX_OK, false);
}

zx_status_t GpioLight::MsgGetSimpleValue(uint32_t index, fidl_txn_t* txn) {
  if (index >= gpio_count_) {
    return fuchsia_hardware_light_LightGetSimpleValue_reply(txn, ZX_ERR_OUT_OF_RANGE, 0);
  }

  uint8_t value;
  auto status = gpios_[index].Read(&value);
  return fuchsia_hardware_light_LightGetSimpleValue_reply(txn, status, value);
}

zx_status_t GpioLight::MsgSetSimpleValue(uint32_t index, uint8_t value, fidl_txn_t* txn) {
  if (index >= gpio_count_) {
    return fuchsia_hardware_light_LightSetSimpleValue_reply(txn, ZX_ERR_OUT_OF_RANGE);
  }

  auto status = gpios_[index].Write(value);
  return fuchsia_hardware_light_LightSetSimpleValue_reply(txn, status);
}

zx_status_t GpioLight::MsgGetRgbValue(uint32_t index, fidl_txn_t* txn) {
  fuchsia_hardware_light_Rgb rgb = {};
  return fuchsia_hardware_light_LightGetRgbValue_reply(txn, ZX_ERR_NOT_SUPPORTED, &rgb);
}

zx_status_t GpioLight::MsgSetRgbValue(uint32_t index, const fuchsia_hardware_light_Rgb* value,
                                      fidl_txn_t* txn) {
  return fuchsia_hardware_light_LightSetRgbValue_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

static fuchsia_hardware_light_Light_ops_t fidl_ops = {
    .GetName =
        [](void* ctx, uint32_t index, fidl_txn_t* txn) {
          return reinterpret_cast<GpioLight*>(ctx)->MsgGetName(txn, index);
        },
    .GetCount = [](void* ctx,
                   fidl_txn_t* txn) { return reinterpret_cast<GpioLight*>(ctx)->MsgGetCount(txn); },
    .HasCapability =
        [](void* ctx, uint32_t index, fuchsia_hardware_light_Capability capability,
           fidl_txn_t* txn) {
          return reinterpret_cast<GpioLight*>(ctx)->MsgHasCapability(index, capability, txn);
        },
    .GetSimpleValue =
        [](void* ctx, uint32_t index, fidl_txn_t* txn) {
          return reinterpret_cast<GpioLight*>(ctx)->MsgGetSimpleValue(index, txn);
        },
    .SetSimpleValue =
        [](void* ctx, uint32_t index, uint8_t value, fidl_txn_t* txn) {
          return reinterpret_cast<GpioLight*>(ctx)->MsgSetSimpleValue(index, value, txn);
        },
    .GetRgbValue =
        [](void* ctx, uint32_t index, fidl_txn_t* txn) {
          return reinterpret_cast<GpioLight*>(ctx)->MsgGetRgbValue(index, txn);
        },
    .SetRgbValue =
        [](void* ctx, uint32_t index, const fuchsia_hardware_light_Rgb* value, fidl_txn_t* txn) {
          return reinterpret_cast<GpioLight*>(ctx)->MsgSetRgbValue(index, value, txn);
        },
};

zx_status_t GpioLight::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_light_Light_dispatch(this, txn, msg, &fidl_ops);
}

void GpioLight::DdkRelease() { delete this; }

zx_status_t GpioLight::Create(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;
  auto dev = std::unique_ptr<GpioLight>(new (&ac) GpioLight(parent));
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

zx_status_t GpioLight::Init() {
  ddk::CompositeProtocolClient composite(parent_);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "GpioLight: Could not get composite protocol\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto component_count = composite.GetComponentCount();
  if (component_count <= 0) {
    return ZX_ERR_INTERNAL;
  }
  // component 0 is platform device, only used for passing metadata.
  gpio_count_ = component_count - 1;

  size_t metadata_size;
  size_t expected = gpio_count_ * kNameLength;
  auto status = device_get_metadata_size(parent(), DEVICE_METADATA_NAME, &metadata_size);
  if (status == ZX_OK) {
    if (expected != metadata_size) {
      zxlogf(ERROR, "%s: expected metadata size %zu, got %zu\n", __func__, expected, metadata_size);
      status = ZX_ERR_INTERNAL;
    }
  }
  if (status == ZX_OK) {
    fbl::AllocChecker ac;
    names_.reset(new (&ac) char[metadata_size], metadata_size);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }

    status =
        device_get_metadata(parent(), DEVICE_METADATA_NAME, names_.get(), metadata_size, &expected);
    if (status != ZX_OK) {
      return status;
    }
  }

  zx_device_t* components[component_count];
  size_t actual;
  composite.GetComponents(components, component_count, &actual);
  if (actual != component_count) {
    return ZX_ERR_INTERNAL;
  }

  fbl::AllocChecker ac;
  auto* gpios = new (&ac) ddk::GpioProtocolClient[gpio_count_];
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  gpios_.reset(gpios, gpio_count_);

  for (uint32_t i = 0; i < gpio_count_; i++) {
    auto* gpio = &gpios_[i];
    auto status = device_get_protocol(components[i + 1], ZX_PROTOCOL_GPIO, gpio);
    if (status != ZX_OK) {
      return status;
    }
    status = gpio->ConfigOut(0);
    if (status != ZX_OK) {
      zxlogf(ERROR, "gpio-light: ConfigOut failed for gpio %u\n", i);
      return status;
    }
  }

  return DdkAdd("gpio-light", DEVICE_ADD_NON_BINDABLE);
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = GpioLight::Create;
  return ops;
}();

}  // namespace gpio_light

ZIRCON_DRIVER_BEGIN(gpio_light, gpio_light::driver_ops, "zircon", "0.1", 4)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_GPIO_LIGHT), ZIRCON_DRIVER_END(gpio_light)
