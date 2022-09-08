// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-cdc-ecm-lib.h"

namespace usb_cdc_ecm {

bool EcmCtx::ParseCdcHeader(usb_cs_header_interface_descriptor_t* header_desc) {
  zxlogf(DEBUG, "Device reports CDC version as 0x%x", header_desc->bcdCDC);
  return header_desc->bcdCDC >= CDC_SUPPORTED_VERSION;
}

bool EcmCtx::ParseCdcEthernetDescriptor(usb_cs_ethernet_interface_descriptor_t* desc) {
  mtu = desc->wMaxSegmentSize;

  // MAC address is stored in a string descriptor in UTF-16 format, so we get one byte of
  // address for each 32 bits of text.
  const size_t expected_str_size = sizeof(usb_string_descriptor_t) + ETH_MAC_SIZE * 4;
  char str_desc_buf[expected_str_size];

  // Read string descriptor for MAC address (string index is in iMACAddress field)
  size_t out_length;
  zx_status_t result =
      usb_get_descriptor(&usbproto, 0, USB_DT_STRING, desc->iMACAddress, (uint8_t*)str_desc_buf,
                         sizeof(str_desc_buf), ZX_TIME_INFINITE, &out_length);
  if (result < 0) {
    zxlogf(ERROR, "Error reading MAC address");
    return false;
  }
  if (out_length != expected_str_size) {
    zxlogf(ERROR, "MAC address string incorrect length (saw %zd, expected %zd)", out_length,
           expected_str_size);
    return false;
  }

  // Convert MAC address to something more machine-friendly
  usb_string_descriptor_t* str_desc = (usb_string_descriptor_t*)str_desc_buf;
  uint8_t* str = str_desc->b_string;
  size_t ndx;
  for (ndx = 0; ndx < ETH_MAC_SIZE * 4; ndx++) {
    if (ndx % 2 == 1) {
      if (str[ndx] != 0) {
        zxlogf(ERROR, "MAC address contains invalid characters");
        return false;
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
      return false;
    }
    if (ndx % 4 == 0) {
      mac_addr[ndx / 4] = (uint8_t)(value << 4);
    } else {
      mac_addr[ndx / 4] |= value;
    }
  }

  zxlogf(INFO, "MAC address is %02X:%02X:%02X:%02X:%02X:%02X", mac_addr[0], mac_addr[1],
         mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  return true;
}

zx_status_t EcmCtx::ParseUsbDescriptor(usb_desc_iter_t* iter, usb_endpoint_descriptor_t** int_ep,
                                       usb_endpoint_descriptor_t** tx_ep,
                                       usb_endpoint_descriptor_t** rx_ep,
                                       usb_interface_descriptor_t** default_ifc,
                                       usb_interface_descriptor_t** data_ifc) {
  zx_status_t result = ZX_ERR_NOT_SUPPORTED;
  usb_descriptor_header_t* desc;
  usb_cs_header_interface_descriptor_t* cdc_header_desc = nullptr;
  usb_cs_ethernet_interface_descriptor_t* cdc_eth_desc = nullptr;
  *int_ep = nullptr;
  *tx_ep = nullptr;
  *rx_ep = nullptr;
  *default_ifc = nullptr;
  *data_ifc = nullptr;
  // TODO: use usb::DescriptorList
  while ((desc = usb_desc_iter_peek(iter)) != nullptr) {
    if (desc->b_descriptor_type == USB_DT_INTERFACE) {
      usb_interface_descriptor_t* ifc_desc = reinterpret_cast<usb_interface_descriptor_t*>(
          usb_desc_iter_get_structure(iter, sizeof(usb_interface_descriptor_t)));
      if (ifc_desc == nullptr) {
        return result;
      }
      if (ifc_desc->b_interface_class == USB_CLASS_CDC) {
        if (ifc_desc->b_num_endpoints == 0) {
          if (*default_ifc) {
            zxlogf(ERROR, "Multiple default interfaces found");
            return result;
          }
          *default_ifc = ifc_desc;
        } else if (ifc_desc->b_num_endpoints == 2) {
          if (*data_ifc) {
            zxlogf(ERROR, "Multiple data interfaces found");
            return result;
          }
          *data_ifc = ifc_desc;
        }
      }
    } else if (desc->b_descriptor_type == USB_DT_CS_INTERFACE) {
      usb_cs_interface_descriptor_t* cs_ifc_desc = reinterpret_cast<usb_cs_interface_descriptor_t*>(
          usb_desc_iter_get_structure(iter, sizeof(usb_cs_interface_descriptor_t)));
      if (cs_ifc_desc == nullptr) {
        return result;
      }
      if (cs_ifc_desc->b_descriptor_sub_type == USB_CDC_DST_HEADER) {
        if (cdc_header_desc != nullptr) {
          zxlogf(ERROR, "Multiple CDC headers");
          return result;
        }
        cdc_header_desc = reinterpret_cast<usb_cs_header_interface_descriptor_t*>(
            usb_desc_iter_get_structure(iter, sizeof(usb_cs_header_interface_descriptor_t)));
      } else if (cs_ifc_desc->b_descriptor_sub_type == USB_CDC_DST_ETHERNET) {
        if (cdc_eth_desc != nullptr) {
          zxlogf(ERROR, "Multiple CDC ethernet descriptors");
          return result;
        }
        cdc_eth_desc = reinterpret_cast<usb_cs_ethernet_interface_descriptor_t*>(
            usb_desc_iter_get_structure(iter, sizeof(usb_cs_ethernet_interface_descriptor_t)));
      }
    } else if (desc->b_descriptor_type == USB_DT_ENDPOINT) {
      usb_endpoint_descriptor_t* endpoint_desc = reinterpret_cast<usb_endpoint_descriptor_t*>(
          usb_desc_iter_get_structure(iter, sizeof(usb_endpoint_descriptor_t)));
      if (endpoint_desc == nullptr) {
        return result;
      }
      if (usb_ep_direction(endpoint_desc) == USB_ENDPOINT_IN &&
          usb_ep_type(endpoint_desc) == USB_ENDPOINT_INTERRUPT) {
        if (*int_ep != nullptr) {
          zxlogf(ERROR, "Multiple interrupt endpoint descriptors");
          return result;
        }
        *int_ep = endpoint_desc;
      } else if (usb_ep_direction(endpoint_desc) == USB_ENDPOINT_OUT &&
                 usb_ep_type(endpoint_desc) == USB_ENDPOINT_BULK) {
        if (*tx_ep != nullptr) {
          zxlogf(ERROR, "Multiple tx endpoint descriptors");
          return result;
        }
        *tx_ep = endpoint_desc;
      } else if (usb_ep_direction(endpoint_desc) == USB_ENDPOINT_IN &&
                 usb_ep_type(endpoint_desc) == USB_ENDPOINT_BULK) {
        if (*rx_ep != nullptr) {
          zxlogf(ERROR, "Multiple rx endpoint descriptors");
          return result;
        }
        *rx_ep = endpoint_desc;
      } else {
        zxlogf(ERROR, "Unrecognized endpoint");
        return result;
      }
    }
    usb_desc_iter_advance(iter);
  }
  if (cdc_header_desc == nullptr || cdc_eth_desc == nullptr) {
    zxlogf(ERROR, "CDC %s descriptor(s) not found",
           cdc_header_desc ? "ethernet"
           : cdc_eth_desc  ? "header"
                           : "ethernet and header");
    return result;
  }
  if (*int_ep == nullptr || *tx_ep == nullptr || *rx_ep == nullptr) {
    zxlogf(ERROR, "Missing one or more required endpoints");
    return result;
  }
  if (*default_ifc == nullptr) {
    zxlogf(ERROR, "Unable to find CDC default interface");
    return result;
  }
  if (*data_ifc == nullptr) {
    zxlogf(ERROR, "Unable to find CDC data interface");
    return result;
  }
  // Parse the information in the CDC descriptors
  if (!ParseCdcHeader(cdc_header_desc)) {
    zxlogf(ERROR, "Unable to parse cdc header");
    return result;
  }

  if (!ParseCdcEthernetDescriptor(cdc_eth_desc)) {
    zxlogf(ERROR, "Unable to parse cdc ethernet descriptor");
    return result;
  }

  return ZX_OK;
}
}  // namespace usb_cdc_ecm
