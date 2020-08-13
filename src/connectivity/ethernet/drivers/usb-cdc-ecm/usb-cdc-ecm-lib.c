// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-cdc-ecm-lib.h"

const char* module_name = "usb-cdc-ecm";

static bool parse_cdc_header(usb_cs_header_interface_descriptor_t* header_desc) {
  // Check for supported CDC version
  zxlogf(DEBUG, "%s: device reports CDC version as 0x%x", module_name, header_desc->bcdCDC);
  return header_desc->bcdCDC >= CDC_SUPPORTED_VERSION;
}

static bool parse_cdc_ethernet_descriptor(ecm_ctx_t* ctx,
                                          usb_cs_ethernet_interface_descriptor_t* desc) {
  ctx->mtu = desc->wMaxSegmentSize;

  // MAC address is stored in a string descriptor in UTF-16 format, so we get one byte of
  // address for each 32 bits of text.
  const size_t expected_str_size = sizeof(usb_string_descriptor_t) + ETH_MAC_SIZE * 4;
  char str_desc_buf[expected_str_size];

  // Read string descriptor for MAC address (string index is in iMACAddress field)
  size_t out_length;
  zx_status_t result =
      usb_get_descriptor(&ctx->usb, 0, USB_DT_STRING, desc->iMACAddress, str_desc_buf,
                         sizeof(str_desc_buf), ZX_TIME_INFINITE, &out_length);
  if (result < 0) {
    zxlogf(ERROR, "%s: error reading MAC address", module_name);
    return false;
  }
  if (out_length != expected_str_size) {
    zxlogf(ERROR, "%s: MAC address string incorrect length (saw %zd, expected %zd)", module_name,
           out_length, expected_str_size);
    return false;
  }

  // Convert MAC address to something more machine-friendly
  usb_string_descriptor_t* str_desc = (usb_string_descriptor_t*)str_desc_buf;
  uint8_t* str = str_desc->bString;
  size_t ndx;
  for (ndx = 0; ndx < ETH_MAC_SIZE * 4; ndx++) {
    if (ndx % 2 == 1) {
      if (str[ndx] != 0) {
        zxlogf(ERROR, "%s: MAC address contains invalid characters", module_name);
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
      zxlogf(ERROR, "%s: MAC address contains invalid characters", module_name);
      return false;
    }
    if (ndx % 4 == 0) {
      ctx->mac_addr[ndx / 4] = value << 4;
    } else {
      ctx->mac_addr[ndx / 4] |= value;
    }
  }

  zxlogf(INFO, "%s: MAC address is %02X:%02X:%02X:%02X:%02X:%02X", module_name, ctx->mac_addr[0],
         ctx->mac_addr[1], ctx->mac_addr[2], ctx->mac_addr[3], ctx->mac_addr[4], ctx->mac_addr[5]);
  return true;
}

zx_status_t parse_usb_descriptor(usb_desc_iter_t* iter, usb_endpoint_descriptor_t** int_ep,
                                 usb_endpoint_descriptor_t** tx_ep,
                                 usb_endpoint_descriptor_t** rx_ep,
                                 usb_interface_descriptor_t** default_ifc,
                                 usb_interface_descriptor_t** data_ifc, ecm_ctx_t* ecm_ctx) {
  zx_status_t result = ZX_ERR_NOT_SUPPORTED;
  usb_descriptor_header_t* desc;
  usb_cs_header_interface_descriptor_t* cdc_header_desc = NULL;
  usb_cs_ethernet_interface_descriptor_t* cdc_eth_desc = NULL;
  *int_ep = NULL;
  *tx_ep = NULL;
  *rx_ep = NULL;
  *default_ifc = NULL;
  *data_ifc = NULL;
  while ((desc = usb_desc_iter_peek(iter)) != NULL) {
    if (desc->bDescriptorType == USB_DT_INTERFACE) {
      usb_interface_descriptor_t* ifc_desc =
          usb_desc_iter_get_structure(iter, sizeof(usb_interface_descriptor_t));
      if (ifc_desc == NULL) {
        goto fail;
      }
      if (ifc_desc->bInterfaceClass == USB_CLASS_CDC) {
        if (ifc_desc->bNumEndpoints == 0) {
          if (*default_ifc) {
            zxlogf(ERROR, "%s: multiple default interfaces found", module_name);
            goto fail;
          }
          *default_ifc = ifc_desc;
        } else if (ifc_desc->bNumEndpoints == 2) {
          if (*data_ifc) {
            zxlogf(ERROR, "%s: multiple data interfaces found", module_name);
            goto fail;
          }
          *data_ifc = ifc_desc;
        }
      }
    } else if (desc->bDescriptorType == USB_DT_CS_INTERFACE) {
      usb_cs_interface_descriptor_t* cs_ifc_desc =
          usb_desc_iter_get_structure(iter, sizeof(usb_cs_interface_descriptor_t));
      if (cs_ifc_desc == NULL) {
        goto fail;
      }
      if (cs_ifc_desc->bDescriptorSubType == USB_CDC_DST_HEADER) {
        if (cdc_header_desc != NULL) {
          zxlogf(ERROR, "%s: multiple CDC headers", module_name);
          goto fail;
        }
        cdc_header_desc =
            usb_desc_iter_get_structure(iter, sizeof(usb_cs_header_interface_descriptor_t));
      } else if (cs_ifc_desc->bDescriptorSubType == USB_CDC_DST_ETHERNET) {
        if (cdc_eth_desc != NULL) {
          zxlogf(ERROR, "%s: multiple CDC ethernet descriptors", module_name);
          goto fail;
        }
        cdc_eth_desc =
            usb_desc_iter_get_structure(iter, sizeof(usb_cs_ethernet_interface_descriptor_t));
      }
    } else if (desc->bDescriptorType == USB_DT_ENDPOINT) {
      usb_endpoint_descriptor_t* endpoint_desc =
          usb_desc_iter_get_structure(iter, sizeof(usb_endpoint_descriptor_t));
      if (endpoint_desc == NULL) {
        goto fail;
      }
      if (usb_ep_direction(endpoint_desc) == USB_ENDPOINT_IN &&
          usb_ep_type(endpoint_desc) == USB_ENDPOINT_INTERRUPT) {
        if (*int_ep != NULL) {
          zxlogf(ERROR, "%s: multiple interrupt endpoint descriptors", module_name);
          goto fail;
        }
        *int_ep = endpoint_desc;
      } else if (usb_ep_direction(endpoint_desc) == USB_ENDPOINT_OUT &&
                 usb_ep_type(endpoint_desc) == USB_ENDPOINT_BULK) {
        if (*tx_ep != NULL) {
          zxlogf(ERROR, "%s: multiple tx endpoint descriptors", module_name);
          goto fail;
        }
        *tx_ep = endpoint_desc;
      } else if (usb_ep_direction(endpoint_desc) == USB_ENDPOINT_IN &&
                 usb_ep_type(endpoint_desc) == USB_ENDPOINT_BULK) {
        if (*rx_ep != NULL) {
          zxlogf(ERROR, "%s: multiple rx endpoint descriptors", module_name);
          goto fail;
        }
        *rx_ep = endpoint_desc;
      } else {
        zxlogf(ERROR, "%s: unrecognized endpoint", module_name);
        goto fail;
      }
    }
    usb_desc_iter_advance(iter);
  }
  if (cdc_header_desc == NULL || cdc_eth_desc == NULL) {
    zxlogf(ERROR, "%s: CDC %s descriptor(s) not found", module_name,
           cdc_header_desc ? "ethernet"
           : cdc_eth_desc  ? "header"
                           : "ethernet and header");
    goto fail;
  }
  if (*int_ep == NULL || *tx_ep == NULL || *rx_ep == NULL) {
    zxlogf(ERROR, "%s: missing one or more required endpoints", module_name);
    goto fail;
  }
  if (*default_ifc == NULL) {
    zxlogf(ERROR, "%s: unable to find CDC default interface", module_name);
    goto fail;
  }
  if (*data_ifc == NULL) {
    zxlogf(ERROR, "%s: unable to find CDC data interface", module_name);
    goto fail;
  }

  // Parse the information in the CDC descriptors
  if (!parse_cdc_header(cdc_header_desc)) {
    zxlogf(ERROR, "%s: unable to parse cdc header", module_name);
    goto fail;
  }
  if (!parse_cdc_ethernet_descriptor(ecm_ctx, cdc_eth_desc)) {
    zxlogf(ERROR, "%s: unable to parse cdc ethernet descriptor", module_name);
    goto fail;
  }
  result = ZX_OK;
fail:
  return result;
}
