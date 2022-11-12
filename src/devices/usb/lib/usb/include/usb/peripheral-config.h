// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_LIB_USB_INCLUDE_USB_PERIPHERAL_CONFIG_H_
#define SRC_DEVICES_USB_LIB_USB_INCLUDE_USB_PERIPHERAL_CONFIG_H_

#include <fidl/fuchsia.hardware.usb.peripheral/cpp/wire.h>
#include <lib/ddk/device.h>

#include <cstdint>
#include <vector>

#include <ddk/usb-peripheral-config.h>
#include <usb/cdc.h>
#include <usb/peripheral.h>
#include <usb/usb.h>

namespace usb {

namespace peripheral = fuchsia_hardware_usb_peripheral;

constexpr char kManufacturer[] = "Zircon";
constexpr char kSerial[] = "0123456789ABCDEF";
constexpr std::string_view kCompositeDeviceConnector = " & ";
constexpr std::string_view kCDCProductDescription = "CDC Ethernet";
constexpr std::string_view kUMSProductDescription = "USB Mass Storage";
constexpr std::string_view kRNDISProductDescription = "RNDIS Ethernet";
constexpr std::string_view kTestProductDescription = "USB Function Test";
constexpr std::string_view kADBProductDescription = "ADB";

constexpr peripheral::wire::FunctionDescriptor kCDCFunctionDescriptor = {
    .interface_class = USB_CLASS_COMM,
    .interface_subclass = USB_CDC_SUBCLASS_ETHERNET,
    .interface_protocol = 0,
};

constexpr peripheral::wire::FunctionDescriptor kUMSFunctionDescriptor = {
    .interface_class = USB_CLASS_MSC,
    .interface_subclass = USB_SUBCLASS_MSC_SCSI,
    .interface_protocol = USB_PROTOCOL_MSC_BULK_ONLY,
};

constexpr peripheral::wire::FunctionDescriptor kRNDISFunctionDescriptor = {
    .interface_class = USB_CLASS_MISC,
    .interface_subclass = USB_SUBCLASS_MSC_RNDIS,
    .interface_protocol = USB_PROTOCOL_MSC_RNDIS_ETHERNET,
};

constexpr peripheral::wire::FunctionDescriptor kADBFunctionDescriptor = {
    .interface_class = USB_CLASS_VENDOR,
    .interface_subclass = USB_SUBCLASS_ADB,
    .interface_protocol = USB_PROTOCOL_ADB,
};

constexpr peripheral::wire::FunctionDescriptor kTestFunctionDescriptor = {
    .interface_class = USB_CLASS_VENDOR,
    .interface_subclass = 0,
    .interface_protocol = 0,
};

// Class for generating USB peripheral config struct.
// Currently supports getting a CDC Ethernet config by default, or parse the boot args
// `driver.usb.peripheral` string to compose different functionality.
class UsbPeripheralConfig {
 public:
  UsbPeripheralConfig() = default;
  // Disable copy construction / assignment.
  // This is because |config_| needs to be freed explicitly.
  UsbPeripheralConfig(const UsbPeripheralConfig&) = delete;
  UsbPeripheralConfig& operator=(const UsbPeripheralConfig&) = delete;

  ~UsbPeripheralConfig() {
    if (config_) {
      free(config_);
    }
  }
  // Create instance by parsing `driver.usb.peripheral` and setup the config for requested
  // functions, or return CDC Ethernet function config.
  static zx_status_t CreateFromBootArgs(zx_device_t* platform_bus,
                                        std::unique_ptr<UsbPeripheralConfig>* out_config);

  const UsbConfig* config() const { return config_; }
  size_t config_size() const { return config_size_; }
  std::vector<uint8_t> config_data() const {
    return std::vector<uint8_t>(reinterpret_cast<uint8_t*>(config_),
                                reinterpret_cast<uint8_t*>(config_) + config_size_);
  }

 private:
  // Helper class to parse boot args. The expected format for `driver.usb.peripheral` values is
  // either a single function name like `cdc` or concatenation of multiple functions with underscore
  // like `cdc_test`.
  zx_status_t ParseBootArgs(zx_device_t* platform_bus);

  // Helper function for determining the pid and product description.
  zx_status_t SetCompositeProductDescription(uint16_t pid);

  // Helper function to allocate |config_| as per alignment requirements.
  zx_status_t AllocateConfig();

  // USB Config structure.
  UsbConfig* config_ = nullptr;
  size_t config_size_ = 0;

  uint16_t pid_ = 0;
  std::string product_desc_;
  std::vector<fuchsia_hardware_usb_peripheral::wire::FunctionDescriptor> function_configs_;
};

}  // namespace usb

#endif  // SRC_DEVICES_USB_LIB_USB_INCLUDE_USB_PERIPHERAL_CONFIG_H_
