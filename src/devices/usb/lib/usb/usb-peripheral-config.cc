// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/zircon-internal/align.h>
#include <zircon/errors.h>

#include <cstdint>
#include <string>

#include <usb/peripheral-config.h>
#include <usb/peripheral.h>

namespace usb {

zx_status_t UsbPeripheralConfig::GetUsbConfigFromBootArgs(UsbConfig **out, size_t *out_size) {
  zx_status_t status = ParseBootArgs();
  if (status != ZX_OK || function_configs_.size() == 0) {
    return status;
  }

  constexpr size_t alignment = alignof(UsbConfig) > __STDCPP_DEFAULT_NEW_ALIGNMENT__
                                   ? alignof(UsbConfig)
                                   : __STDCPP_DEFAULT_NEW_ALIGNMENT__;
  size_t config_size =
      sizeof(UsbConfig) +
      function_configs_.size() * sizeof(fuchsia_hardware_usb_peripheral::wire::FunctionDescriptor);

  UsbConfig *config =
      reinterpret_cast<UsbConfig *>(aligned_alloc(alignment, ZX_ROUNDUP(config_size, alignment)));
  if (!config) {
    return ZX_ERR_NO_MEMORY;
  }
  config->vid = GOOGLE_USB_VID;
  config->pid = pid_;
  std::strncpy(config->manufacturer, kManufacturer, sizeof(kManufacturer));
  std::strncpy(config->serial, kSerial, sizeof(kSerial));
  std::strncpy(config->product, product_desc_.c_str(), product_desc_.size());
  for (uint32_t idx = 0; idx < function_configs_.size(); idx++) {
    config->functions[idx] = function_configs_[idx];
  }

  *out = config;
  *out_size = config_size;
  return ZX_OK;
}

zx_status_t UsbPeripheralConfig::ParseBootArgs() {
  char bootarg[32];
  zx_status_t status = device_get_variable(platform_bus_, "driver.usb.peripheral", bootarg,
                                           sizeof(bootarg), nullptr);
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
  return status;
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
