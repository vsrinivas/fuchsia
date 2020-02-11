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
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/composite.h>
#include <fbl/alloc_checker.h>

namespace gpio_light {

void GpioLight::GetName(uint32_t index, GetNameCompleter::Sync completer) {
  if (index >= gpio_count_) {
    completer.Reply(ZX_ERR_OUT_OF_RANGE, ::fidl::StringView(nullptr, 0));
    return;
  }

  if (names_.size() > 0) {
    auto* name = names_.data() + index * kNameLength;
    completer.Reply(ZX_OK, ::fidl::StringView(name, strlen(name) + 1));
  } else {
    // Return "gpio-X" if no metadata was provided.
    char name[20];
    snprintf(name, sizeof(name), "gpio-%u\n", index);
    completer.Reply(ZX_OK, ::fidl::StringView(name, strlen(name) + 1));
  }
}

void GpioLight::GetCount(GetCountCompleter::Sync completer) { completer.Reply(gpio_count_); }

void GpioLight::HasCapability(uint32_t index,
                              llcpp::fuchsia::hardware::light::Capability capability,
                              HasCapabilityCompleter::Sync completer) {
  if (index >= gpio_count_) {
    completer.Reply(ZX_ERR_OUT_OF_RANGE, false);
    return;
  }
  completer.Reply(ZX_OK, false);
}

void GpioLight::GetSimpleValue(uint32_t index, GetSimpleValueCompleter::Sync completer) {
  if (index >= gpio_count_) {
    completer.Reply(ZX_ERR_OUT_OF_RANGE, 0);
    return;
  }

  uint8_t value;
  auto status = gpios_[index].Read(&value);
  completer.Reply(status, value);
}

void GpioLight::SetSimpleValue(uint32_t index, uint8_t value,
                               SetSimpleValueCompleter::Sync completer) {
  if (index >= gpio_count_) {
    completer.Reply(ZX_ERR_OUT_OF_RANGE);
    return;
  }

  auto status = gpios_[index].Write(value);
  completer.Reply(status);
}

void GpioLight::GetRgbValue(uint32_t index, GetRgbValueCompleter::Sync completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
}

void GpioLight::SetRgbValue(uint32_t index, llcpp::fuchsia::hardware::light::Rgb value,
                            SetRgbValueCompleter::Sync completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED);
}

zx_status_t GpioLight::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  llcpp::fuchsia::hardware::light::Light::Dispatch(this, msg, &transaction);
  return transaction.Status();
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

    status = device_get_metadata(parent(), DEVICE_METADATA_NAME, names_.data(), metadata_size,
                                 &expected);
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
