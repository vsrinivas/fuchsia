// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "xhci-root-hub.h"

#include <lib/ddk/debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/hw/usb.h>

#include <fbl/alloc_checker.h>
#include <usb/usb-request.h>

#include "xhci-device-manager.h"
#include "xhci.h"

namespace usb_xhci {

#define MANUFACTURER_STRING 1
#define PRODUCT_STRING_2 2
#define PRODUCT_STRING_3 3

static const uint8_t xhci_language_list[] = {4, /* b_length */ USB_DT_STRING, 0x09, 0x04,
                                             /* language ID */};
static const uint8_t xhci_manufacturer_string[] =  // "Zircon"
    {16, /* b_length */ USB_DT_STRING, 'Z', 0, 'i', 0, 'r', 0, 'c', 0, 'o', 0, 'n', 0, 0, 0};
static const uint8_t xhci_product_string_2[] =  // "USB 2.0 Root Hub"
    {
        36,  /* b_length */ USB_DT_STRING,
        'U', 0,
        'S', 0,
        'B', 0,
        ' ', 0,
        '2', 0,
        '.', 0,
        '0', 0,
        ' ', 0,
        'R', 0,
        'o', 0,
        'o', 0,
        't', 0,
        ' ', 0,
        'H', 0,
        'u', 0,
        'b', 0,
        0,   0,
};
static const uint8_t xhci_product_string_3[] =  // "USB 3.0 Root Hub"
    {
        36,  /* b_length */ USB_DT_STRING,
        'U', 0,
        'S', 0,
        'B', 0,
        ' ', 0,
        '3', 0,
        '.', 0,
        '0', 0,
        ' ', 0,
        'R', 0,
        'o', 0,
        'o', 0,
        't', 0,
        ' ', 0,
        'H', 0,
        'u', 0,
        'b', 0,
        0,   0,
};

static const uint8_t* xhci_rh_string_table[] = {
    xhci_language_list,
    xhci_manufacturer_string,
    xhci_product_string_2,
    xhci_product_string_3,
};

// device descriptor for USB 2.0 root hub
static const usb_device_descriptor_t xhci_rh_device_desc_2 = {
    .b_length = sizeof(usb_device_descriptor_t),
    .b_descriptor_type = USB_DT_DEVICE,
    .bcd_usb = htole16(0x0200),
    .b_device_class = USB_CLASS_HUB,
    .b_device_sub_class = 0,
    .b_device_protocol = 1,  // Single TT
    .b_max_packet_size0 = 64,
    .id_vendor = htole16(0x18D1),
    .id_product = htole16(0xA002),
    .bcd_device = htole16(0x0100),
    .i_manufacturer = MANUFACTURER_STRING,
    .i_product = PRODUCT_STRING_2,
    .i_serial_number = 0,
    .b_num_configurations = 1,
};

// device descriptor for USB 3.1 root hub
static const usb_device_descriptor_t xhci_rh_device_desc_3 = {
    .b_length = sizeof(usb_device_descriptor_t),
    .b_descriptor_type = USB_DT_DEVICE,
    .bcd_usb = htole16(0x0300),
    .b_device_class = USB_CLASS_HUB,
    .b_device_sub_class = 0,
    .b_device_protocol = 1,  // Single TT
    .b_max_packet_size0 = 64,
    .id_vendor = htole16(0x18D1),
    .id_product = htole16(0xA003),
    .bcd_device = htole16(0x0100),
    .i_manufacturer = MANUFACTURER_STRING,
    .i_product = PRODUCT_STRING_3,
    .i_serial_number = 0,
    .b_num_configurations = 1,
};

// device descriptors for our virtual root hub devices
static const usb_device_descriptor_t* xhci_rh_device_descs[] = {
    (usb_device_descriptor_t*)&xhci_rh_device_desc_2,
    (usb_device_descriptor_t*)&xhci_rh_device_desc_3,
};

// we are currently using the same configuration descriptors for both USB 2.0 and 3.0 root hubs
// this is not actually correct, but our usb-hub driver isn't sophisticated enough to notice
static const struct {
  usb_configuration_descriptor_t config;
  usb_interface_descriptor_t intf;
  usb_endpoint_descriptor_t endp;
} xhci_rh_config_desc = {
    .config =
        {
            .bLength = sizeof(usb_configuration_descriptor_t),
            .bDescriptorType = USB_DT_CONFIG,
            .wTotalLength = htole16(sizeof(xhci_rh_config_desc)),
            .bNumInterfaces = 1,
            .bConfigurationValue = 1,
            .iConfiguration = 0,
            .bmAttributes = 0xE0,  // self powered
            .bMaxPower = 0,
        },
    .intf =
        {
            .b_length = sizeof(usb_interface_descriptor_t),
            .b_descriptor_type = USB_DT_INTERFACE,
            .b_interface_number = 0,
            .b_alternate_setting = 0,
            .b_num_endpoints = 1,
            .b_interface_class = USB_CLASS_HUB,
            .b_interface_sub_class = 0,
            .b_interface_protocol = 0,
            .i_interface = 0,
        },
    .endp =
        {
            .b_length = sizeof(usb_endpoint_descriptor_t),
            .b_descriptor_type = USB_DT_ENDPOINT,
            .b_endpoint_address = USB_ENDPOINT_IN | 1,
            .bm_attributes = USB_ENDPOINT_INTERRUPT,
            .w_max_packet_size = htole16(4),
            .b_interval = 12,
        },
};

// speeds for our virtual root hub devices
static const usb_speed_t xhci_rh_speeds[] = {
    USB_SPEED_HIGH,
    USB_SPEED_SUPER,
};

static void print_portsc(int port, uint32_t portsc) {
  zxlogf(TRACE, "port %d:", port);
  if (portsc & PORTSC_CCS)
    zxlogf(TRACE, " CCS");
  if (portsc & PORTSC_PED)
    zxlogf(TRACE, " PED");
  if (portsc & PORTSC_OCA)
    zxlogf(TRACE, " OCA");
  if (portsc & PORTSC_PR)
    zxlogf(TRACE, " PR");
  uint32_t pls = (portsc >> PORTSC_PLS_START) & ((1 << PORTSC_PLS_BITS) - 1);
  switch (pls) {
    case 0:
      zxlogf(TRACE, " U0");
      break;
    case 1:
      zxlogf(TRACE, " U1");
      break;
    case 2:
      zxlogf(TRACE, " U2");
      break;
    case 3:
      zxlogf(TRACE, " U3");
      break;
    case 4:
      zxlogf(TRACE, " Disabled");
      break;
    case 5:
      zxlogf(TRACE, " RxDetect");
      break;
    case 6:
      zxlogf(TRACE, " Inactive");
      break;
    case 7:
      zxlogf(TRACE, " Polling");
      break;
    case 8:
      zxlogf(TRACE, " Recovery");
      break;
    case 9:
      zxlogf(TRACE, " Hot Reset");
      break;
    case 10:
      zxlogf(TRACE, " Compliance Mode");
      break;
    case 11:
      zxlogf(TRACE, " Test Mode");
      break;
    case 15:
      zxlogf(TRACE, " Resume");
      break;
    default:
      zxlogf(TRACE, " PLS%d", pls);
      break;
  }
  if (portsc & PORTSC_PP)
    zxlogf(TRACE, " PP");
  uint32_t speed = (portsc >> PORTSC_SPEED_START) & ((1 << PORTSC_SPEED_BITS) - 1);
  switch (speed) {
    case 1:
      zxlogf(TRACE, " FULL_SPEED");
      break;
    case 2:
      zxlogf(TRACE, " LOW_SPEED");
      break;
    case 3:
      zxlogf(TRACE, " HIGH_SPEED");
      break;
    case 4:
      zxlogf(TRACE, " SUPER_SPEED");
      break;
  }
  uint32_t pic = (portsc >> PORTSC_PIC_START) & ((1 << PORTSC_PIC_BITS) - 1);
  zxlogf(TRACE, " PIC%d", pic);
  if (portsc & PORTSC_LWS)
    zxlogf(TRACE, " LWS");
  if (portsc & PORTSC_CSC)
    zxlogf(TRACE, " CSC");
  if (portsc & PORTSC_PEC)
    zxlogf(TRACE, " PEC");
  if (portsc & PORTSC_WRC)
    zxlogf(TRACE, " WRC");
  if (portsc & PORTSC_OCC)
    zxlogf(TRACE, " OCC");
  if (portsc & PORTSC_PRC)
    zxlogf(TRACE, " PRC");
  if (portsc & PORTSC_PLC)
    zxlogf(TRACE, " PLC");
  if (portsc & PORTSC_CEC)
    zxlogf(TRACE, " CEC");
  if (portsc & PORTSC_CAS)
    zxlogf(TRACE, " CAS");
  if (portsc & PORTSC_WCE)
    zxlogf(TRACE, " WCE");
  if (portsc & PORTSC_WDE)
    zxlogf(TRACE, " WDE");
  if (portsc & PORTSC_WOE)
    zxlogf(TRACE, " WOE");
  if (portsc & PORTSC_DR)
    zxlogf(TRACE, " DR");
  if (portsc & PORTSC_WPR)
    zxlogf(TRACE, " WPR");
  zxlogf(TRACE, "");
}

static void xhci_reset_port(xhci_t* xhci, xhci_root_hub_t* rh, int rh_port_index) {
  volatile uint32_t* portsc = &xhci->op_regs->port_regs[rh_port_index].portsc;
  uint32_t temp = XHCI_READ32(portsc);
  temp = (temp & PORTSC_CONTROL_BITS) | PORTSC_PR;
  if (rh->speed == USB_SPEED_SUPER) {
    temp |= PORTSC_WPR;
  }
  XHCI_WRITE32(portsc, temp);

  int port_index = xhci->rh_port_map[rh_port_index];
  usb_port_status_t* status = &rh->port_status[port_index];
  status->w_port_status |= USB_PORT_RESET;
  status->w_port_change |= USB_C_PORT_RESET;
}

zx_status_t xhci_root_hub_init(xhci_t* xhci, int rh_index) {
  xhci_root_hub_t* rh = &xhci->root_hubs[rh_index];
  auto* rh_port_map = xhci->rh_map.data();
  uint8_t rh_ports = xhci->rh_num_ports;

  list_initialize(&rh->pending_intr_reqs);

  rh->device_desc = xhci_rh_device_descs[rh_index];
  rh->config_desc = (usb_configuration_descriptor_t*)&xhci_rh_config_desc;

  // first count number of ports
  uint8_t port_count = 0;
  for (size_t i = 0; i < rh_ports; i++) {
    if (rh_port_map[i] == rh_index) {
      port_count++;
    }
  }
  rh->num_ports = port_count;

  fbl::AllocChecker ac;
  rh->port_status.reset(new (&ac) usb_port_status_t[port_count], port_count);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  rh->port_map.reset(new (&ac) uint8_t[port_count], port_count);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  // build map from virtual port index to actual port index
  uint8_t port_index = 0;
  for (uint8_t i = 0; i < rh_ports; i++) {
    if (rh_port_map[i] == rh_index) {
      xhci->rh_port_map[i] = port_index;
      rh->port_map[port_index] = i;
      port_index++;
    }
  }

  return ZX_OK;
}

static zx_status_t xhci_start_root_hub(xhci_t* xhci, xhci_root_hub_t* rh, int rh_index) {
  rh->speed = xhci_rh_speeds[rh_index];

  // Notify bus driver that our emulated hub exists
  return xhci_add_device(xhci, xhci->max_slots + rh_index + 1, 0, rh->speed);
}

zx_status_t xhci_start_root_hubs(xhci_t* xhci) {
  zxlogf(DEBUG, "xhci_start_root_hubs");

  for (int i = 0; i < XHCI_RH_COUNT; i++) {
    zx_status_t status = xhci_start_root_hub(xhci, &xhci->root_hubs[i], i);
    if (status != ZX_OK) {
      zxlogf(ERROR, "xhci_start_root_hub(%d) failed: %d", i, status);
      return status;
    }
  }

  return ZX_OK;
}

void xhci_stop_root_hubs(xhci_t* xhci) {
  zxlogf(DEBUG, "xhci_stop_root_hubs");

  volatile xhci_port_regs_t* port_regs = xhci->op_regs->port_regs;
  for (uint8_t i = 0; i < xhci->rh_num_ports; i++) {
    uint32_t portsc = XHCI_READ32(&port_regs[i].portsc);
    portsc &= PORTSC_CONTROL_BITS;
    portsc |= PORTSC_PED;  // disable port
    portsc &= PORTSC_PP;   // power off port
    XHCI_WRITE32(&port_regs[i].portsc, portsc);
  }

  for (int i = 0; i < XHCI_RH_COUNT; i++) {
    usb_request_t* req;
    xhci_usb_request_internal_t* req_int = nullptr;
    xhci_root_hub_t* rh = &xhci->root_hubs[i];
    while ((req_int = list_remove_tail_type(&rh->pending_intr_reqs, xhci_usb_request_internal_t,
                                            node)) != nullptr) {
      req = XHCI_INTERNAL_TO_USB_REQ(req_int);
      usb_request_complete(req, ZX_ERR_IO_NOT_PRESENT, 0, &req_int->complete_cb);
    }
  }
}

static zx_status_t xhci_rh_get_descriptor(xhci_t* xhci, uint8_t request_type, xhci_root_hub_t* rh,
                                          uint16_t value, uint16_t index, size_t length,
                                          usb_request_t* req) {
  uint8_t type = request_type & USB_TYPE_MASK;
  uint8_t recipient = request_type & USB_RECIP_MASK;
  xhci_usb_request_internal_t* req_int = USB_REQ_TO_XHCI_INTERNAL(req);

  if (type == USB_TYPE_STANDARD && recipient == USB_RECIP_DEVICE) {
    auto desc_type = static_cast<uint8_t>(value >> 8);
    if (desc_type == USB_DT_DEVICE && index == 0) {
      if (length > sizeof(usb_device_descriptor_t))
        length = sizeof(usb_device_descriptor_t);
      size_t copy_size = usb_request_copy_to(req, rh->device_desc, length, 0);
      ZX_ASSERT(copy_size == length);
      usb_request_complete(req, ZX_OK, length, &req_int->complete_cb);
      return ZX_OK;
    } else if (desc_type == USB_DT_CONFIG && index == 0) {
      uint16_t desc_length = le16toh(rh->config_desc->wTotalLength);
      if (length > desc_length)
        length = desc_length;
      size_t copy_size = usb_request_copy_to(req, rh->config_desc, length, 0);
      ZX_ASSERT(copy_size == length);
      usb_request_complete(req, ZX_OK, length, &req_int->complete_cb);
      return ZX_OK;
    } else if (value >> 8 == USB_DT_STRING) {
      auto string_index = static_cast<uint8_t>(value & 0xFF);
      if (string_index < countof(xhci_rh_string_table)) {
        const uint8_t* string = xhci_rh_string_table[string_index];
        if (length > string[0])
          length = string[0];

        size_t copy_size = usb_request_copy_to(req, string, length, 0);
        ZX_ASSERT(copy_size == length);
        usb_request_complete(req, ZX_OK, length, &req_int->complete_cb);
        return ZX_OK;
      }
    }
  } else if (type == USB_TYPE_CLASS && recipient == USB_RECIP_DEVICE) {
    if ((value == USB_HUB_DESC_TYPE_SS << 8 || value == USB_HUB_DESC_TYPE << 8) && index == 0) {
      // return hub descriptor
      usb_hub_descriptor_t desc;
      memset(&desc, 0, sizeof(desc));
      desc.b_desc_length = sizeof(desc);
      desc.b_descriptor_type = static_cast<uint8_t>(value >> 8);
      desc.b_nbr_ports = rh->num_ports;
      desc.b_power_on2_pwr_good = 0;
      // TODO - fill in other stuff. But usb-hub driver doesn't need anything else at this point.

      if (length > sizeof(desc))
        length = sizeof(desc);
      size_t copy_size = usb_request_copy_to(req, &desc, length, 0);
      ZX_ASSERT(copy_size == length);
      usb_request_complete(req, ZX_OK, length, &req_int->complete_cb);
      return ZX_OK;
    }
  }

  zxlogf(ERROR, "xhci_rh_get_descriptor unsupported value: %d index: %d", value, index);
  usb_request_complete(req, ZX_ERR_NOT_SUPPORTED, 0, &req_int->complete_cb);
  return ZX_ERR_NOT_SUPPORTED;
}

// handles control requests for virtual root hub devices
static zx_status_t xhci_rh_control(xhci_t* xhci, xhci_root_hub_t* rh, usb_setup_t* setup,
                                   usb_request_t* req) {
  uint8_t request_type = setup->bm_request_type;
  uint8_t request = setup->b_request;
  uint16_t value = le16toh(setup->w_value);
  uint16_t index = le16toh(setup->w_index);
  xhci_usb_request_internal_t* req_int = USB_REQ_TO_XHCI_INTERNAL(req);

  zxlogf(TRACE, "xhci_rh_control type: 0x%02X req: %d value: %d index: %d length: %d", request_type,
         request, value, index, le16toh(setup->w_length));

  if ((request_type & USB_DIR_MASK) == USB_DIR_IN && request == USB_REQ_GET_DESCRIPTOR) {
    return xhci_rh_get_descriptor(xhci, request_type, rh, value, index, le16toh(setup->w_length),
                                  req);
  } else if ((request_type & ~USB_DIR_MASK) == (USB_TYPE_CLASS | USB_RECIP_PORT)) {
    // index is 1-based port number
    if (index < 1 || index > rh->num_ports) {
      usb_request_complete(req, ZX_ERR_INVALID_ARGS, 0, &req_int->complete_cb);
      return ZX_OK;
    }
    auto rh_port_index = rh->port_map[index - 1];
    auto port_index = static_cast<uint8_t>(index - 1);

    if (request == USB_REQ_SET_FEATURE) {
      if (value == USB_FEATURE_PORT_POWER) {
        // nothing to do - root hub ports are already powered
        usb_request_complete(req, ZX_OK, 0, &req_int->complete_cb);
        return ZX_OK;
      } else if (value == USB_FEATURE_PORT_RESET) {
        xhci_reset_port(xhci, rh, rh_port_index);
        usb_request_complete(req, ZX_OK, 0, &req_int->complete_cb);
        return ZX_OK;
      }
    } else if (request == USB_REQ_CLEAR_FEATURE) {
      auto* change_bits = &rh->port_status[port_index].w_port_change;

      switch (value) {
        case USB_FEATURE_C_PORT_CONNECTION:
          *change_bits &= static_cast<uint16_t>(~USB_C_PORT_CONNECTION);
          break;
        case USB_FEATURE_C_PORT_ENABLE:
          *change_bits &= static_cast<uint16_t>(~USB_C_PORT_ENABLE);
          break;
        case USB_FEATURE_C_PORT_SUSPEND:
          *change_bits &= static_cast<uint16_t>(~USB_C_PORT_SUSPEND);
          break;
        case USB_FEATURE_C_PORT_OVER_CURRENT:
          *change_bits &= static_cast<uint16_t>(~USB_C_PORT_OVER_CURRENT);
          break;
        case USB_FEATURE_C_PORT_RESET:
          *change_bits &= static_cast<uint16_t>(~USB_C_PORT_RESET);
          break;
      }

      usb_request_complete(req, ZX_OK, 0, &req_int->complete_cb);
      return ZX_OK;
    } else if ((request_type & USB_DIR_MASK) == USB_DIR_IN && request == USB_REQ_GET_STATUS &&
               value == 0) {
      usb_port_status_t* status = &rh->port_status[port_index];
      size_t length = req->header.length;
      if (length > sizeof(*status))
        length = sizeof(*status);
      size_t copy_size = usb_request_copy_to(req, status, length, 0);
      ZX_ASSERT(copy_size == length);
      usb_request_complete(req, ZX_OK, length, &req_int->complete_cb);
      return ZX_OK;
    }
  } else if (request_type == (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE) &&
             request == USB_REQ_SET_CONFIGURATION && req->header.length == 0) {
    // nothing to do here
    usb_request_complete(req, ZX_OK, 0, &req_int->complete_cb);
    return ZX_OK;
  }

  zxlogf(ERROR, "unsupported root hub control request type: 0x%02X req: %d value: %d index: %d",
         request_type, request, value, index);

  usb_request_complete(req, ZX_ERR_NOT_SUPPORTED, 0, &req_int->complete_cb);
  return ZX_ERR_NOT_SUPPORTED;
}

static void xhci_rh_handle_intr_req(xhci_t* xhci, xhci_root_hub_t* rh, usb_request_t* req) {
  zxlogf(TRACE, "xhci_rh_handle_intr_req");
  uint8_t status_bits[128 / 8];
  bool have_status = 0;
  uint8_t* ptr = status_bits;
  int bit = 1;  // 0 is for hub status, so start at bit 1

  memset(status_bits, 0, sizeof(status_bits));

  for (uint32_t i = 0; i < rh->num_ports; i++) {
    usb_port_status_t* status = &rh->port_status[i];
    if (status->w_port_change) {
      *ptr |= static_cast<uint8_t>(1 << bit);
      have_status = true;
    }
    if (++bit == 8) {
      ptr++;
      bit = 0;
    }
  }

  if (have_status) {
    size_t length = req->header.length;
    xhci_usb_request_internal_t* req_int = USB_REQ_TO_XHCI_INTERNAL(req);
    if (length > sizeof(status_bits))
      length = sizeof(status_bits);
    size_t copy_size = usb_request_copy_to(req, status_bits, length, 0);
    ZX_ASSERT(copy_size == length);
    usb_request_complete(req, ZX_OK, length, &req_int->complete_cb);
  } else {
    // queue transaction until we have something to report
    xhci_add_to_list_tail(xhci, &rh->pending_intr_reqs, req);
  }
}

zx_status_t xhci_rh_usb_request_queue(xhci_t* xhci, usb_request_t* req, int rh_index) {
  zxlogf(TRACE, "xhci_rh_usb_request_queue rh_index: %d", rh_index);

  xhci_root_hub_t* rh = &xhci->root_hubs[rh_index];
  xhci_usb_request_internal_t* req_int = USB_REQ_TO_XHCI_INTERNAL(req);

  uint8_t ep_index = xhci_endpoint_index(req->header.ep_address);
  if (ep_index == 0) {
    return xhci_rh_control(xhci, rh, &req->setup, req);
  } else if (ep_index == 2) {
    xhci_rh_handle_intr_req(xhci, rh, req);
    return ZX_OK;
  }

  usb_request_complete(req, ZX_ERR_NOT_SUPPORTED, 0, &req_int->complete_cb);
  return ZX_ERR_NOT_SUPPORTED;
}

void xhci_handle_root_hub_change(xhci_t* xhci) {
  volatile xhci_port_regs_t* port_regs = xhci->op_regs->port_regs;

  zxlogf(DEBUG, "xhci_handle_root_hub_change");

  for (uint8_t i = 0; i < xhci->rh_num_ports; i++) {
    uint32_t portsc = XHCI_READ32(&port_regs[i].portsc);
    uint32_t speed =
        (portsc & XHCI_MASK(PORTSC_SPEED_START, PORTSC_SPEED_BITS)) >> PORTSC_SPEED_START;
    uint32_t status_bits = portsc & PORTSC_STATUS_BITS;

    if (zxlog_level_enabled(TRACE)) {
      print_portsc(i, portsc);
    }

    if (status_bits) {
      bool connected = !!(portsc & PORTSC_CCS);
      bool enabled = !!(portsc & PORTSC_PED);

      // set change bits to acknowledge
      XHCI_WRITE32(&port_regs[i].portsc, (portsc & PORTSC_CONTROL_BITS) | status_bits);

      // map index to virtual root hub and port number
      int rh_index = xhci->rh_map[i];
      xhci_root_hub_t* rh = &xhci->root_hubs[rh_index];
      int port_index = xhci->rh_port_map[i];
      usb_port_status_t* status = &rh->port_status[port_index];

      if (portsc & PORTSC_CSC) {
        // connect status change
        zxlogf(DEBUG, "port %d PORTSC_CSC connected: %d", i, connected);
        if (connected) {
          status->w_port_status |= USB_PORT_CONNECTION;
        } else {
          if (status->w_port_status & USB_PORT_ENABLE) {
            status->w_port_change |= USB_C_PORT_ENABLE;
          }
          status->w_port_status = 0;
        }
        status->w_port_change |= USB_C_PORT_CONNECTION;
      }
      if (portsc & PORTSC_PRC) {
        // port reset change
        zxlogf(DEBUG, "port %d PORTSC_PRC enabled: %d", i, enabled);
        if (enabled) {
          status->w_port_status &= static_cast<uint16_t>(~USB_PORT_RESET);
          status->w_port_change |= USB_C_PORT_RESET;
          if (!(status->w_port_status & USB_PORT_ENABLE)) {
            status->w_port_status |= USB_PORT_ENABLE;
            status->w_port_change |= USB_C_PORT_ENABLE;
          }

          if (speed == USB_SPEED_LOW) {
            status->w_port_status |= USB_PORT_LOW_SPEED;
          } else if (speed == USB_SPEED_HIGH) {
            status->w_port_status |= USB_PORT_HIGH_SPEED;
          }
        }
      }

      if (status->w_port_change) {
        usb_request_t* req;
        if (xhci_remove_from_list_head(xhci, &rh->pending_intr_reqs, &req)) {
          xhci_rh_handle_intr_req(xhci, rh, req);
        }
      }
    }
  }
}

}  // namespace usb_xhci
