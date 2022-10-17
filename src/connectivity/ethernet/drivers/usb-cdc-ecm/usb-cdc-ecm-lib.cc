// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-cdc-ecm-lib.h"

#include <lib/fit/defer.h>
#include <zircon/errors.h>

#include "fuchsia/hardware/usb/descriptor/c/banjo.h"
#include "usb/usb.h"

namespace usb_cdc_ecm {

zx::result<MacAddress> UsbCdcDescriptorParser::ParseMacAddress(
    usb::UsbDevice& usb, const usb_cs_ethernet_interface_descriptor_t* desc) {
  // Read string descriptor for MAC address (string index is in iMACAddress field)
  size_t out_length;
  uint8_t str_desc_buf[kExpectedStringSize];
  zx_status_t status = usb.GetDescriptor(0, USB_DT_STRING, desc->iMACAddress, str_desc_buf,
                                         sizeof(str_desc_buf), ZX_TIME_INFINITE, &out_length);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Error reading MAC address");
    return zx::error(status);
  }
  if (out_length != kExpectedStringSize) {
    zxlogf(ERROR, "MAC address string incorrect length (saw %zd, expected %zd)", out_length,
           kExpectedStringSize);
    return zx::error(ZX_ERR_IO);
  }

  // Convert MAC address to something more machine-friendly
  auto str_desc = reinterpret_cast<usb_string_descriptor_t*>(str_desc_buf);
  uint8_t* str = str_desc->b_string;
  size_t ndx;
  MacAddress mac_addr;
  for (ndx = 0; ndx < ETH_MAC_SIZE * 4; ndx++) {
    if (ndx % 2 == 1) {
      if (str[ndx] != 0) {
        zxlogf(ERROR, "MAC address contains invalid characters");
        return zx::error(ZX_ERR_IO);
      }
      continue;
    }
    uint8_t value;
    if (str[ndx] >= '0' && str[ndx] <= '9') {
      value = str[ndx] - '0';
    } else if (str[ndx] >= 'A' && str[ndx] <= 'F') {
      value = (str[ndx] - 'A') + 0xa;
    } else {
      zxlogf(ERROR, "MAC address contains invalid characters");
      return zx::error(ZX_ERR_IO);
    }
    if (ndx % 4 == 0) {
      mac_addr[ndx / 4] = (uint8_t)(value << 4);
    } else {
      mac_addr[ndx / 4] |= value;
    }
  }

  zxlogf(INFO, "MAC address is %02X:%02X:%02X:%02X:%02X:%02X", mac_addr[0], mac_addr[1],
         mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  return zx::ok(mac_addr);
}

zx::result<UsbCdcDescriptorParser> UsbCdcDescriptorParser::Parse(usb::UsbDevice& usb) {
  std::optional<usb::InterfaceList> interfaces;
  zx_status_t status = usb::InterfaceList::Create(usb, false, &interfaces);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  std::optional<EcmEndpoint> int_ep;
  std::optional<EcmEndpoint> tx_ep;
  std::optional<EcmEndpoint> rx_ep;
  std::optional<EcmInterface> default_ifc;
  std::optional<EcmInterface> data_ifc;

  // Find default interface.
  for (const usb::Interface& interface : *interfaces) {
    const usb_interface_descriptor_t* desc = interface.descriptor();
    if (desc->b_interface_class != USB_CLASS_CDC) {
      continue;
    }
    if (desc->b_num_endpoints != 0) {
      continue;
    }
    if (default_ifc.has_value()) {
      zxlogf(ERROR, "Multiple default interfaces found");
      return zx::error(ZX_ERR_NOT_SUPPORTED);
    }
    default_ifc = EcmInterface(desc);
  }

  // Find data interface.
  for (const usb::Interface& interface : *interfaces) {
    const usb_interface_descriptor_t* desc = interface.descriptor();
    if (desc->b_interface_class != USB_CLASS_CDC) {
      continue;
    }
    if (desc->b_num_endpoints != 2) {
      continue;
    }
    if (data_ifc.has_value()) {
      zxlogf(ERROR, "Multiple data interfaces found");
      return zx::error(ZX_ERR_NOT_SUPPORTED);
    }
    data_ifc = EcmInterface(desc);

    for (const auto& endpoint : interface.GetEndpointList()) {
      const usb_endpoint_descriptor_t* endpoint_desc = endpoint.descriptor();
      if (usb_ep_direction(endpoint_desc) == USB_ENDPOINT_OUT &&
          usb_ep_type(endpoint_desc) == USB_ENDPOINT_BULK) {
        if (tx_ep.has_value()) {
          zxlogf(ERROR, "Multiple tx endpoint descriptors");
          return zx::error(ZX_ERR_NOT_SUPPORTED);
        }
        tx_ep = EcmEndpoint(endpoint_desc);
      } else if (usb_ep_direction(endpoint_desc) == USB_ENDPOINT_IN &&
                 usb_ep_type(endpoint_desc) == USB_ENDPOINT_BULK) {
        if (rx_ep.has_value()) {
          zxlogf(ERROR, "Multiple rx endpoint descriptors");
          return zx::error(ZX_ERR_NOT_SUPPORTED);
        }
        rx_ep = EcmEndpoint(endpoint_desc);
      } else {
        zxlogf(ERROR, "Unrecognized endpoint");
        return zx::error(ZX_ERR_NOT_SUPPORTED);
      }
    }
  }

  // Find communications interface, which has CDC headers and interrupt endpoint.
  const usb_cs_header_interface_descriptor_t* cdc_header_desc = nullptr;
  const usb_cs_ethernet_interface_descriptor_t* cdc_eth_desc = nullptr;
  for (const usb::Interface& interface : *interfaces) {
    if (interface.descriptor()->b_interface_class != USB_CLASS_COMM) {
      continue;
    }

    for (auto& descriptor : interface.GetDescriptorList()) {
      if (descriptor.b_descriptor_type != USB_DT_CS_INTERFACE) {
        continue;
      }

      const usb_cs_interface_descriptor_t* cs_ifc_desc =
          reinterpret_cast<const usb_cs_interface_descriptor_t*>(&descriptor);

      if (cs_ifc_desc->b_descriptor_sub_type == USB_CDC_DST_HEADER) {
        if (cdc_header_desc != nullptr) {
          zxlogf(ERROR, "Multiple CDC headers");
          return zx::error(ZX_ERR_NOT_SUPPORTED);
        }
        cdc_header_desc =
            reinterpret_cast<const usb_cs_header_interface_descriptor_t*>(&descriptor);
      } else if (cs_ifc_desc->b_descriptor_sub_type == USB_CDC_DST_ETHERNET) {
        if (cdc_eth_desc != nullptr) {
          zxlogf(ERROR, "Multiple CDC ethernet descriptors");
          return zx::error(ZX_ERR_NOT_SUPPORTED);
        }
        cdc_eth_desc = reinterpret_cast<const usb_cs_ethernet_interface_descriptor_t*>(&descriptor);
      }
    }

    for (const auto& endpoint : interface.GetEndpointList()) {
      const usb_endpoint_descriptor_t* endpoint_desc = endpoint.descriptor();
      if (usb_ep_direction(endpoint_desc) == USB_ENDPOINT_IN &&
          usb_ep_type(endpoint_desc) == USB_ENDPOINT_INTERRUPT) {
        if (int_ep.has_value()) {
          zxlogf(ERROR, "Multiple interrupt endpoint descriptors");
          return zx::error(ZX_ERR_NOT_SUPPORTED);
        }
        int_ep = EcmEndpoint(endpoint_desc);
      }
    }
  }

  if (cdc_header_desc == nullptr || cdc_eth_desc == nullptr) {
    zxlogf(ERROR, "CDC %s descriptor(s) not found",
           cdc_header_desc ? "ethernet"
           : cdc_eth_desc  ? "header"
                           : "ethernet and header");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  if (!int_ep.has_value() || !tx_ep.has_value() || !rx_ep.has_value()) {
    zxlogf(ERROR, "Missing one or more required endpoints");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  if (!default_ifc.has_value()) {
    zxlogf(ERROR, "Unable to find CDC default interface");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  if (!data_ifc.has_value()) {
    zxlogf(ERROR, "Unable to find CDC data interface");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // Parse the information in the CDC descriptors
  zxlogf(DEBUG, "Device reports CDC version as 0x%x", cdc_header_desc->bcdCDC);
  if (cdc_header_desc->bcdCDC < kCdcSupportedVersion) {
    zxlogf(ERROR, "Unable to parse cdc header");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  const uint16_t mtu = cdc_eth_desc->wMaxSegmentSize;

  auto mac_addr = UsbCdcDescriptorParser::ParseMacAddress(usb, cdc_eth_desc);
  if (mac_addr.is_error()) {
    zxlogf(ERROR, "Unable to parse cdc ethernet descriptor");
    return mac_addr.take_error();
  }

  return zx::ok(UsbCdcDescriptorParser(int_ep.value(), tx_ep.value(), rx_ep.value(),
                                       default_ifc.value(), data_ifc.value(), mtu,
                                       mac_addr.value()));
}

}  // namespace usb_cdc_ecm
