// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/zircon-internal/align.h>
#include <zircon/errors.h>

#include <cstdint>
#include <memory>
#include <string>

#include <usb/peripheral-config.h>
#include <usb/peripheral.h>

#include "lib/ddk/driver.h"

namespace usb {

zx_status_t UsbPeripheralConfig::CreateFromBootArgs(
    zx_device_t *platform_bus, std::unique_ptr<UsbPeripheralConfig> *out_config) {
  auto peripheral_config = std::make_unique<UsbPeripheralConfig>();
  zx_status_t status = peripheral_config->ParseBootArgs(platform_bus);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to get Usb peripheral config from bootargs: %d", status);
    return status;
  }
  *out_config = std::move(peripheral_config);
  return ZX_OK;
}

zx_status_t UsbPeripheralConfig::ParseBootArgs(zx_device_t *platform_bus) {
  char bootarg[32];
  zx_status_t status =
      device_get_variable(platform_bus, "driver.usb.peripheral", bootarg, sizeof(bootarg), nullptr);
  if (status == ZX_ERR_NOT_FOUND) {
    // No bootargs set for usb peripheral config. Use cdc function as default.
    std::strcpy(bootarg, "cdc");
  } else if (status != ZX_OK) {
    // Return error for all other errors.
    zxlogf(ERROR, "Failed to get driver.usb.peripheral config: %d", status);
    return status;
  }

  // driver.usb.peripheral can be used for specifying either a single function - cdc/rndis/ums etc.,
  // or specify a composite interface by joining function strings with a underscore -
  // cdc_test/cdc_adb etc.
  std::string config(bootarg);
  while (!config.empty()) {
    auto end = config.find('_');
    std::string function = config.substr(0, end);
    if (end != std::string::npos) {
      config = config.substr(function.size() + 1);
    } else {
      config = "";
    }
    if (function == "cdc") {
      function_configs_.push_back(kCDCFunctionDescriptor);
      status = SetCompositeProductDescription(GOOGLE_USB_CDC_PID);
    } else if (function == "ums") {
      function_configs_.push_back(kUMSFunctionDescriptor);
      status = SetCompositeProductDescription(GOOGLE_USB_UMS_PID);
    } else if (function == "rndis") {
      function_configs_.push_back(kRNDISFunctionDescriptor);
      status = SetCompositeProductDescription(GOOGLE_USB_RNDIS_PID);
    } else if (function == "adb") {
      function_configs_.push_back(kADBFunctionDescriptor);
      status = SetCompositeProductDescription(GOOGLE_USB_ADB_PID);
    } else if (function == "test") {
      function_configs_.push_back(kTestFunctionDescriptor);
      status = SetCompositeProductDescription(GOOGLE_USB_FUNCTION_TEST_PID);
    } else {
      zxlogf(ERROR, "Function not supported: %s", function.c_str());
      status = ZX_ERR_INVALID_ARGS;
    }
    if (status != ZX_OK) {
      break;
    }
  }

  if (status != ZX_OK) {
    return status;
  }

  if (function_configs_.empty()) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (status = AllocateConfig(); status != ZX_OK) {
    return status;
  }

  config_->vid = GOOGLE_USB_VID;
  config_->pid = pid_;
  std::strncpy(config_->manufacturer, kManufacturer, sizeof(kManufacturer));
  std::strncpy(config_->serial, kSerial, sizeof(kSerial));
  std::strncpy(config_->product, product_desc_.c_str(), product_desc_.size());
  for (uint32_t idx = 0; idx < function_configs_.size(); idx++) {
    config_->functions[idx] = function_configs_[idx];
  }

  return ZX_OK;
}

zx_status_t UsbPeripheralConfig::AllocateConfig() {
  if (config_) {
    return ZX_ERR_ALREADY_EXISTS;
  }
  constexpr size_t alignment = alignof(UsbConfig) > __STDCPP_DEFAULT_NEW_ALIGNMENT__
                                   ? alignof(UsbConfig)
                                   : __STDCPP_DEFAULT_NEW_ALIGNMENT__;
  config_size_ =
      sizeof(UsbConfig) +
      function_configs_.size() * sizeof(fuchsia_hardware_usb_peripheral::wire::FunctionDescriptor);

  config_ =
      reinterpret_cast<UsbConfig *>(aligned_alloc(alignment, ZX_ROUNDUP(config_size_, alignment)));
  if (!config_) {
    return ZX_ERR_NO_MEMORY;
  }
  return ZX_OK;
}

zx_status_t UsbPeripheralConfig::SetCompositeProductDescription(uint16_t pid) {
  if (pid_ == 0) {
    switch (pid) {
      case GOOGLE_USB_CDC_PID:
        product_desc_ = kCDCProductDescription;
        break;
      case GOOGLE_USB_RNDIS_PID:
        product_desc_ = kRNDISProductDescription;
        break;
      case GOOGLE_USB_UMS_PID:
        product_desc_ = kUMSProductDescription;
        break;
      case GOOGLE_USB_ADB_PID:
        product_desc_ = kADBProductDescription;
        break;
      case GOOGLE_USB_FUNCTION_TEST_PID:
        product_desc_ = kTestProductDescription;
        break;
      default:
        zxlogf(ERROR, "Invalid pid: %d", pid);
        return ZX_ERR_WRONG_TYPE;
    }
    pid_ = pid;
  } else {
    product_desc_ += kCompositeDeviceConnector;
    if (pid_ == GOOGLE_USB_CDC_PID && pid == GOOGLE_USB_FUNCTION_TEST_PID) {
      pid_ = GOOGLE_USB_CDC_AND_FUNCTION_TEST_PID;
      product_desc_ += kTestProductDescription;
    } else if (pid_ == GOOGLE_USB_CDC_PID && pid == GOOGLE_USB_ADB_PID) {
      pid_ = GOOGLE_USB_CDC_AND_ADB_PID;
      product_desc_ += kADBProductDescription;
    } else {
      zxlogf(ERROR, "No matching pid for this combination: %d + %d", pid_, pid);
      return ZX_ERR_WRONG_TYPE;
    }
  }
  return ZX_OK;
}

}  // namespace usb
