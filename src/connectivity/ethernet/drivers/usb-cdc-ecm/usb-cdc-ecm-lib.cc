// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-cdc-ecm-lib.h"

#include <lib/fit/defer.h>
#include <zircon/errors.h>

namespace usb_cdc_ecm {

zx::status<MacAddress> UsbCdcDescriptorParser::ParseMacAddress(
    usb_protocol_t* usb, usb_cs_ethernet_interface_descriptor_t* desc) {
  // MAC address is stored in a string descriptor in UTF-16 format, so we get one byte of
  // address for each 32 bits of text.
  const size_t expected_str_size = sizeof(usb_string_descriptor_t) + ETH_MAC_SIZE * 4;
  char str_desc_buf[expected_str_size];

  // Read string descriptor for MAC address (string index is in iMACAddress field)
  size_t out_length;
  zx_status_t result =
      usb_get_descriptor(usb, 0, USB_DT_STRING, desc->iMACAddress, (uint8_t*)str_desc_buf,
                         sizeof(str_desc_buf), ZX_TIME_INFINITE, &out_length);
  if (result != ZX_OK) {
    zxlogf(ERROR, "Error reading MAC address");
    return zx::error(result);
  }
  if (out_length != expected_str_size) {
    zxlogf(ERROR, "MAC address string incorrect length (saw %zd, expected %zd)", out_length,
           expected_str_size);
    return zx::error(ZX_ERR_IO);
  }

  // Convert MAC address to something more machine-friendly
  usb_string_descriptor_t* str_desc = (usb_string_descriptor_t*)str_desc_buf;
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

zx::status<UsbCdcDescriptorParser> UsbCdcDescriptorParser::Parse(usb_protocol_t* usb) {
  usb_desc_iter_t iter = {};
  zx_status_t result = usb_desc_iter_init(usb, &iter);
  if (result != ZX_OK) {
    return zx::error(result);
  }
  auto iter_cleanup = fit::defer([&iter]() { usb_desc_iter_release(&iter); });

  usb_endpoint_descriptor_t* int_ep = nullptr;
  usb_endpoint_descriptor_t* tx_ep = nullptr;
  usb_endpoint_descriptor_t* rx_ep = nullptr;
  usb_interface_descriptor_t* default_ifc = nullptr;
  usb_interface_descriptor_t* data_ifc = nullptr;

  usb_descriptor_header_t* desc;
  usb_cs_header_interface_descriptor_t* cdc_header_desc = nullptr;
  usb_cs_ethernet_interface_descriptor_t* cdc_eth_desc = nullptr;
  // TODO: use usb::DescriptorList
  while ((desc = usb_desc_iter_peek(&iter)) != nullptr) {
    if (desc->b_descriptor_type == USB_DT_INTERFACE) {
      usb_interface_descriptor_t* ifc_desc = reinterpret_cast<usb_interface_descriptor_t*>(
          usb_desc_iter_get_structure(&iter, sizeof(usb_interface_descriptor_t)));
      if (ifc_desc == nullptr) {
        return zx::error(ZX_ERR_NOT_SUPPORTED);
      }
      if (ifc_desc->b_interface_class == USB_CLASS_CDC) {
        if (ifc_desc->b_num_endpoints == 0) {
          if (default_ifc) {
            zxlogf(ERROR, "Multiple default interfaces found");
            return zx::error(ZX_ERR_NOT_SUPPORTED);
          }
          default_ifc = ifc_desc;
        } else if (ifc_desc->b_num_endpoints == 2) {
          if (data_ifc) {
            zxlogf(ERROR, "Multiple data interfaces found");
            return zx::error(ZX_ERR_NOT_SUPPORTED);
          }
          data_ifc = ifc_desc;
        }
      }
    } else if (desc->b_descriptor_type == USB_DT_CS_INTERFACE) {
      usb_cs_interface_descriptor_t* cs_ifc_desc = reinterpret_cast<usb_cs_interface_descriptor_t*>(
          usb_desc_iter_get_structure(&iter, sizeof(usb_cs_interface_descriptor_t)));
      if (cs_ifc_desc == nullptr) {
        return zx::error(ZX_ERR_NOT_SUPPORTED);
      }
      if (cs_ifc_desc->b_descriptor_sub_type == USB_CDC_DST_HEADER) {
        if (cdc_header_desc != nullptr) {
          zxlogf(ERROR, "Multiple CDC headers");
          return zx::error(ZX_ERR_NOT_SUPPORTED);
        }
        cdc_header_desc = reinterpret_cast<usb_cs_header_interface_descriptor_t*>(
            usb_desc_iter_get_structure(&iter, sizeof(usb_cs_header_interface_descriptor_t)));
      } else if (cs_ifc_desc->b_descriptor_sub_type == USB_CDC_DST_ETHERNET) {
        if (cdc_eth_desc != nullptr) {
          zxlogf(ERROR, "Multiple CDC ethernet descriptors");
          return zx::error(ZX_ERR_NOT_SUPPORTED);
        }
        cdc_eth_desc = reinterpret_cast<usb_cs_ethernet_interface_descriptor_t*>(
            usb_desc_iter_get_structure(&iter, sizeof(usb_cs_ethernet_interface_descriptor_t)));
      }
    } else if (desc->b_descriptor_type == USB_DT_ENDPOINT) {
      usb_endpoint_descriptor_t* endpoint_desc = reinterpret_cast<usb_endpoint_descriptor_t*>(
          usb_desc_iter_get_structure(&iter, sizeof(usb_endpoint_descriptor_t)));
      if (endpoint_desc == nullptr) {
        return zx::error(ZX_ERR_NOT_SUPPORTED);
      }
      if (usb_ep_direction(endpoint_desc) == USB_ENDPOINT_IN &&
          usb_ep_type(endpoint_desc) == USB_ENDPOINT_INTERRUPT) {
        if (int_ep != nullptr) {
          zxlogf(ERROR, "Multiple interrupt endpoint descriptors");
          return zx::error(ZX_ERR_NOT_SUPPORTED);
        }
        int_ep = endpoint_desc;
      } else if (usb_ep_direction(endpoint_desc) == USB_ENDPOINT_OUT &&
                 usb_ep_type(endpoint_desc) == USB_ENDPOINT_BULK) {
        if (tx_ep != nullptr) {
          zxlogf(ERROR, "Multiple tx endpoint descriptors");
          return zx::error(ZX_ERR_NOT_SUPPORTED);
        }
        tx_ep = endpoint_desc;
      } else if (usb_ep_direction(endpoint_desc) == USB_ENDPOINT_IN &&
                 usb_ep_type(endpoint_desc) == USB_ENDPOINT_BULK) {
        if (rx_ep != nullptr) {
          zxlogf(ERROR, "Multiple rx endpoint descriptors");
          return zx::error(ZX_ERR_NOT_SUPPORTED);
        }
        rx_ep = endpoint_desc;
      } else {
        zxlogf(ERROR, "Unrecognized endpoint");
        return zx::error(ZX_ERR_NOT_SUPPORTED);
      }
    }
    usb_desc_iter_advance(&iter);
  }
  if (cdc_header_desc == nullptr || cdc_eth_desc == nullptr) {
    zxlogf(ERROR, "CDC %s descriptor(s) not found",
           cdc_header_desc ? "ethernet"
           : cdc_eth_desc  ? "header"
                           : "ethernet and header");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  if (int_ep == nullptr || tx_ep == nullptr || rx_ep == nullptr) {
    zxlogf(ERROR, "Missing one or more required endpoints");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  if (default_ifc == nullptr) {
    zxlogf(ERROR, "Unable to find CDC default interface");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  if (data_ifc == nullptr) {
    zxlogf(ERROR, "Unable to find CDC data interface");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // Parse the information in the CDC descriptors
  zxlogf(DEBUG, "Device reports CDC version as 0x%x", cdc_header_desc->bcdCDC);
  if (cdc_header_desc->bcdCDC < kCdcSupportedVersion) {
    zxlogf(ERROR, "Unable to parse cdc header");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  uint16_t mtu = cdc_eth_desc->wMaxSegmentSize;

  auto mac_addr = UsbCdcDescriptorParser::ParseMacAddress(usb, cdc_eth_desc);
  if (mac_addr.is_error()) {
    zxlogf(ERROR, "Unable to parse cdc ethernet descriptor");
    return mac_addr.take_error();
  }

  return zx::ok(UsbCdcDescriptorParser(EcmEndpoint(int_ep), EcmEndpoint(tx_ep), EcmEndpoint(rx_ep),
                                       EcmInterface(default_ifc), EcmInterface(data_ifc), mtu,
                                       mac_addr.value()));
}

}  // namespace usb_cdc_ecm
