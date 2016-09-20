// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/iotxn.h>
#include <ddk/protocol/usb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "xhci.h"
#include "xhci-device-manager.h"

//#define TRACE 1
#include "xhci-debug.h"

#define MANUFACTURER_STRING 1
#define PRODUCT_STRING_2    2
#define PRODUCT_STRING_3    3

static const uint8_t xhci_language_list[] =
    { 4, /* bLength */ USB_DT_STRING, 0x09, 0x04, /* language ID */ };
static const uint8_t xhci_manufacturer_string [] = // "Magenta"
    { 18, /* bLength */ USB_DT_STRING, 'M', 0, 'a', 0, 'g', 0, 'e', 0, 'n', 0, 't', 0, 'a', 0, 0, 0 };
static const uint8_t xhci_product_string_2 [] = // "USB 2.0 Root Hub"
    { 36, /* bLength */ USB_DT_STRING, 'U', 0, 'S', 0, 'B', 0, ' ', 0, '2', 0, '.', 0, '0', 0,' ', 0,
                        'R', 0, 'o', 0, 'o', 0, 't', 0, ' ', 0, 'H', 0, 'u', 0, 'b', 0, 0, 0, };
static const uint8_t xhci_product_string_3 [] = // "USB 3.0 Root Hub"
    { 36, /* bLength */ USB_DT_STRING, 'U', 0, 'S', 0, 'B', 0, ' ', 0, '3', 0, '.', 0, '0', 0,' ', 0,
                        'R', 0, 'o', 0, 'o', 0, 't', 0, ' ', 0, 'H', 0, 'u', 0, 'b', 0, 0, 0, };

static const uint8_t* xhci_rh_string_table[] = {
    xhci_language_list,
    xhci_manufacturer_string,
    xhci_product_string_2,
    xhci_product_string_3,
};

// device descriptor for USB 2.0 root hub
// represented as a byte array to avoid endianness issues
static const uint8_t xhci_rh_device_desc_2[sizeof(usb_device_descriptor_t)] = {
    sizeof(usb_device_descriptor_t),    // bLength
    USB_DT_DEVICE,                      // bDescriptorType
    0x00, 0x02,                         // bcdUSB = 2.0
    USB_CLASS_HUB,                      // bDeviceClass
    0,                                  // bDeviceSubClass
    1,                                  // bDeviceProtocol = Single TT
    64,                                 // bMaxPacketSize0
    0xD1, 0x18,                         // idVendor = 0x18D1 (Google)
    0x02, 0xA0,                         // idProduct = 0xA002
    0x00, 0x01,                         // bcdDevice = 1.0
    MANUFACTURER_STRING,                // iManufacturer
    PRODUCT_STRING_2,                   // iProduct
    0,                                  // iSerialNumber
    1,                                  // bNumConfigurations
};

// device descriptor for USB 3.1 root hub
// represented as a byte array to avoid endianness issues
static const uint8_t xhci_rh_device_desc_3[sizeof(usb_device_descriptor_t)] = {
    sizeof(usb_device_descriptor_t),    // bLength
    USB_DT_DEVICE,                      // bDescriptorType
    0x00, 0x03,                         // bcdUSB = 3.0
    USB_CLASS_HUB,                      // bDeviceClass
    0,                                  // bDeviceSubClass
    1,                                  // bDeviceProtocol = Single TT
    64,                                 // bMaxPacketSize0
    0xD1, 0x18,                         // idVendor = 0x18D1 (Google)
    0x03, 0xA0,                         // idProduct = 0xA003
    0x00, 0x01,                         // bcdDevice = 1.0
    MANUFACTURER_STRING,                // iManufacturer
    PRODUCT_STRING_3,                   // iProduct
    0,                                  // iSerialNumber
    1,                                  // bNumConfigurations
};

// device descriptors for our virtual root hub devices
static const usb_device_descriptor_t* xhci_rh_device_descs[] = {
    (usb_device_descriptor_t *)xhci_rh_device_desc_2,
    (usb_device_descriptor_t *)xhci_rh_device_desc_3,
};

#define CONFIG_DESC_SIZE sizeof(usb_configuration_descriptor_t) + \
                         sizeof(usb_interface_descriptor_t) + \
                         sizeof(usb_endpoint_descriptor_t)

// we are currently using the same configuration descriptors for both USB 2.0 and 3.0 root hubs
// this is not actually correct, but our usb-hub driver isn't sophisticated enough to notice
static const uint8_t xhci_rh_config_desc[CONFIG_DESC_SIZE] = {
    // config descriptor
    sizeof(usb_configuration_descriptor_t),    // bLength
    USB_DT_CONFIG,                             // bDescriptorType
    CONFIG_DESC_SIZE, 0,                       // wTotalLength
    1,                                         // bNumInterfaces
    1,                                         // bConfigurationValue
    0,                                         // iConfiguration
    0xE0,                                      // bmAttributes = self powered
    0,                                         // bMaxPower
    // interface descriptor
    sizeof(usb_interface_descriptor_t),         // bLength
    USB_DT_INTERFACE,                           // bDescriptorType
    0,                                          // bInterfaceNumber
    0,                                          // bAlternateSetting
    1,                                          // bNumEndpoints
    USB_CLASS_HUB,                              // bInterfaceClass
    0,                                          // bInterfaceSubClass
    0,                                          // bInterfaceProtocol
    0,                                          // iInterface
    // endpoint descriptor
    sizeof(usb_endpoint_descriptor_t),          // bLength
    USB_DT_ENDPOINT,                            // bDescriptorType
    USB_ENDPOINT_IN | 1,                        // bEndpointAddress
    USB_ENDPOINT_INTERRUPT,                     // bmAttributes
    4, 0,                                       // wMaxPacketSize
    12,                                         // bInterval
};

// speeds for our virtual root hub devices
static const usb_speed_t xhci_rh_speeds[] = {
    USB_SPEED_HIGH,
    USB_SPEED_SUPER,
};

static void xhci_reset_port(xhci_t* xhci, xhci_root_hub_t* rh, int rh_port_index) {
    volatile uint32_t* portsc = &xhci->op_regs->port_regs[rh_port_index].portsc;
    uint32_t temp = XHCI_READ32(portsc);
    temp = (temp & PORTSC_CONTROL_BITS) | PORTSC_PR;
    XHCI_WRITE32(portsc, temp);

    int port_index = xhci->rh_port_map[rh_port_index];
    usb_port_status_t* status = &rh->port_status[port_index];
    status->wPortStatus |= USB_PORT_RESET;
    status->wPortChange |= USB_PORT_RESET;
}

mx_status_t xhci_root_hub_init(xhci_t* xhci, int rh_index) {
    xhci_root_hub_t* rh = &xhci->root_hubs[rh_index];
    const uint8_t* rh_port_map = xhci->rh_map;
    size_t rh_ports = xhci->rh_num_ports;

    list_initialize(&rh->pending_intr_reqs);

    rh->device_desc = xhci_rh_device_descs[rh_index];
    rh->config_desc = (usb_configuration_descriptor_t *)xhci_rh_config_desc;

    // first count number of ports
    int port_count = 0;
    for (size_t i = 0; i < rh_ports; i++) {
        if (rh_port_map[i] == rh_index) {
            port_count++;
        }
    }
    rh->num_ports = port_count;

    rh->port_status = (usb_port_status_t *)calloc(port_count, sizeof(usb_port_status_t));
    if (!rh->port_status) return ERR_NO_MEMORY;

    rh->port_map = calloc(port_count, sizeof(uint8_t));
    if (!rh->port_map) return ERR_NO_MEMORY;

    // build map from virtual port index to actual port index
    int port_index = 0;
    for (size_t i = 0; i < rh_ports; i++) {
        if (rh_port_map[i] == rh_index) {
            xhci->rh_port_map[i] = port_index;
            rh->port_map[port_index] = i;
            port_index++;
        }
    }

    return NO_ERROR;
}

void xhci_root_hub_free(xhci_root_hub_t* rh) {
    free(rh->port_map);
    free(rh->port_status);
}

static mx_status_t xhci_start_root_hub(xhci_t* xhci, xhci_root_hub_t* rh, int rh_index) {
    usb_device_descriptor_t* device_desc = malloc(sizeof(usb_device_descriptor_t));
    if (!device_desc) {
        return ERR_NO_MEMORY;
    }
    usb_configuration_descriptor_t* config_desc = (usb_configuration_descriptor_t *)malloc(CONFIG_DESC_SIZE);
    if (!config_desc) {
        free(device_desc);
        return ERR_NO_MEMORY;
    }

    memcpy(device_desc, rh->device_desc, sizeof(usb_device_descriptor_t));
    memcpy(config_desc, rh->config_desc, le16toh(rh->config_desc->wTotalLength));

    // Notify bus driver that our emulated hub exists
    return xhci_add_device(xhci, xhci->max_slots + rh_index + 1, 0, xhci_rh_speeds[rh_index]);
}

mx_status_t xhci_start_root_hubs(xhci_t* xhci) {
    xprintf("xhci_start_root_hubs\n");

    // power cycle all the root hubs first to make sure we start off with a clean slate
    for (uint32_t i = 0; i < xhci->rh_num_ports; i++) {
        volatile uint32_t* portsc = &xhci->op_regs->port_regs[i].portsc;
        uint32_t temp = XHCI_READ32(portsc);
        // power off
        temp = (temp & PORTSC_CONTROL_BITS) & ~PORTSC_PP;
        XHCI_WRITE32(portsc, temp);
        xhci_wait_bits(portsc, PORTSC_PP, 0);

        // power port back on
        temp = XHCI_READ32(portsc);
        temp = (temp & PORTSC_CONTROL_BITS) | PORTSC_PP;
        XHCI_WRITE32(portsc, temp);
        xhci_wait_bits(portsc, PORTSC_PP, PORTSC_PP);
    }

    for (int i = 0; i < XHCI_RH_COUNT; i++) {
        mx_status_t status = xhci_start_root_hub(xhci, &xhci->root_hubs[i], i);
        if (status != NO_ERROR) {
            printf("xhci_start_root_hub(%d) failed: %d\n", i, status);
            return status;
        }
    }
    return NO_ERROR;
}

static mx_status_t xhci_rh_get_descriptor(uint8_t request_type, xhci_root_hub_t* rh, uint16_t value,
                                          uint16_t index, size_t length, iotxn_t* txn) {
    uint8_t type = request_type & USB_TYPE_MASK;
    uint8_t recipient = request_type & USB_RECIP_MASK;

    if (type == USB_TYPE_STANDARD && recipient == USB_RECIP_DEVICE) {
        uint8_t desc_type = value >> 8;
        if (desc_type == USB_DT_DEVICE && index == 0) {
            if (length > sizeof(usb_device_descriptor_t)) length = sizeof(usb_device_descriptor_t);
            txn->ops->copyto(txn, rh->device_desc, length, 0);
            txn->ops->complete(txn, NO_ERROR, length);
            return NO_ERROR;
        } else if (desc_type == USB_DT_CONFIG && index == 0) {
            uint16_t desc_length = le16toh(rh->config_desc->wTotalLength);
            if (length > desc_length) length = desc_length;
            txn->ops->copyto(txn, rh->config_desc, length, 0);
            txn->ops->complete(txn, NO_ERROR, length);
            return NO_ERROR;
        } else if (value >> 8 == USB_DT_STRING) {
            uint8_t string_index = value & 0xFF;
            if (string_index < countof(xhci_rh_string_table)) {
                const uint8_t* string = xhci_rh_string_table[string_index];
                if (length > string[0]) length = string[0];

                txn->ops->copyto(txn, string, length, 0);
                txn->ops->complete(txn, NO_ERROR, length);
                return NO_ERROR;
            }
        }
    }
    else if (type == USB_TYPE_CLASS && recipient == USB_RECIP_DEVICE) {
        if ((value == USB_HUB_DESC_TYPE_SS << 8 || value == USB_HUB_DESC_TYPE << 8) && index == 0) {
            // return hub descriptor
            usb_hub_descriptor_t desc;
            memset(&desc, 0, sizeof(desc));
            desc.bDescLength = sizeof(desc);
            desc.bDescriptorType = value >> 8;
            desc.bNbrPorts = rh->num_ports;
            desc.bPowerOn2PwrGood = 0;
            // TODO - fill in other stuff. But usb-hub driver doesn't need anything else at this point.

            if (length > sizeof(desc)) length = sizeof(desc);
            txn->ops->copyto(txn, &desc, length, 0);
            txn->ops->complete(txn, NO_ERROR, length);
            return NO_ERROR;
        }
    }

    printf("xhci_rh_get_descriptor unsupported value: %d index: %d\n", value, index);
    txn->ops->complete(txn, ERR_NOT_SUPPORTED, 0);
    return ERR_NOT_SUPPORTED;
}


// handles control requests for virtual root hub devices
static mx_status_t xhci_rh_control(xhci_t* xhci, xhci_root_hub_t* rh, usb_setup_t* setup, iotxn_t* txn) {
    uint8_t request_type = setup->bmRequestType;
    uint8_t request = setup->bRequest;
    uint16_t value = le16toh(setup->wValue);
    uint16_t index = le16toh(setup->wIndex);

    xprintf("xhci_rh_control type: 0x%02X req: %d value: %d index: %d length: %d\n",
            request_type, request, value, index, le16toh(setup->wLength));

    if ((request_type & USB_DIR_MASK) == USB_DIR_IN && request == USB_REQ_GET_DESCRIPTOR) {
        return xhci_rh_get_descriptor(request_type, rh, value, index, le16toh(setup->wLength), txn);
    } else if ((request_type & ~USB_DIR_MASK) == (USB_TYPE_CLASS | USB_RECIP_PORT)) {
        // index is 1-based port number
        if (index < 1 || index >= rh->num_ports) {
            txn->ops->complete(txn, ERR_INVALID_ARGS, 0);
            return NO_ERROR;
        }
        int rh_port_index = rh->port_map[index - 1];
        int port_index = index - 1;

        if (request == USB_REQ_SET_FEATURE) {
            if (value == USB_FEATURE_PORT_POWER) {
                // nothing to do - root hub ports are already powered
                txn->ops->complete(txn, NO_ERROR, 0);
                return NO_ERROR;
            } else if (value == USB_FEATURE_PORT_RESET) {
                xhci_reset_port(xhci, rh, rh_port_index);
                txn->ops->complete(txn, NO_ERROR, 0);
                return NO_ERROR;
            }
        } else if (request == USB_REQ_CLEAR_FEATURE) {
            uint16_t* change_bits = &rh->port_status[port_index].wPortChange;

            switch (value) {
                case USB_FEATURE_C_PORT_CONNECTION:
                    *change_bits &= ~USB_PORT_CONNECTION;
                    break;
                case USB_FEATURE_C_PORT_ENABLE:
                    *change_bits &= ~USB_PORT_ENABLE;
                    break;
                case USB_FEATURE_C_PORT_SUSPEND:
                    *change_bits &= ~USB_PORT_SUSPEND;
                    break;
                case USB_FEATURE_C_PORT_OVER_CURRENT:
                    *change_bits &= ~USB_PORT_OVER_CURRENT;
                    break;
                case USB_FEATURE_C_PORT_RESET:
                    *change_bits &= ~USB_PORT_RESET;
                    break;
            }
            txn->ops->complete(txn, NO_ERROR, 0);
            return NO_ERROR;
        } else if ((request_type & USB_DIR_MASK) == USB_DIR_IN &&
                   request == USB_REQ_GET_STATUS && value == 0) {
            usb_port_status_t* status = &rh->port_status[port_index];
            size_t length = txn->length;
            if (length > sizeof(*status)) length = sizeof(*status);
            txn->ops->copyto(txn, status, length, 0);
            txn->ops->complete(txn, NO_ERROR, length);
            return NO_ERROR;
        }
    } else if (request_type == (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE) &&
               request == USB_REQ_SET_CONFIGURATION && txn->length == 0) {
        // nothing to do here
        txn->ops->complete(txn, NO_ERROR, 0);
        return NO_ERROR;
    }

    printf("unsupported root hub control request type: 0x%02X req: %d value: %d index: %d\n",
           request_type, request, value, index);

    txn->ops->complete(txn, ERR_NOT_SUPPORTED, 0);
    return ERR_NOT_SUPPORTED;
}

static void xhci_rh_handle_intr_req(xhci_root_hub_t* rh, iotxn_t* txn) {
    uint8_t status_bits[128 / 8];
    bool have_status = 0;
    uint8_t* ptr = status_bits;
    int bit = 1;    // 0 is for hub status, so start at bit 1

    memset(status_bits, 0, sizeof(status_bits));

    for (uint32_t i = 0; i < rh->num_ports; i++) {
        usb_port_status_t* status = &rh->port_status[i];
        if (status->wPortChange) {
            *ptr |= (1 << bit);
            have_status = true;
        }
        if (++bit == 8) {
            ptr++;
            bit = 0;
        }
    }

    if (have_status) {
        size_t length = txn->length;
        if (length > sizeof(status_bits)) length = sizeof(status_bits);
        txn->ops->copyto(txn, status_bits, length, 0);
        txn->ops->complete(txn, NO_ERROR, length);
    } else {
        // queue transaction until we have something to report
        list_add_tail(&rh->pending_intr_reqs, &txn->node);
    }
}

mx_status_t xhci_rh_iotxn_queue(xhci_t* xhci, iotxn_t* txn, int rh_index) {
    xprintf("xhci_rh_iotxn_queue rh_index: %d\n", rh_index);

    usb_protocol_data_t* data = iotxn_pdata(txn, usb_protocol_data_t);
    xhci_root_hub_t* rh = &xhci->root_hubs[rh_index];

    uint8_t ep_index = xhci_endpoint_index(data->ep_address);
    if (ep_index == 0) {
        return xhci_rh_control(xhci, rh, &data->setup, txn);
    } else if (ep_index == 2) {
        xhci_rh_handle_intr_req(rh, txn);
        return NO_ERROR;
    }

    txn->ops->complete(txn, ERR_NOT_SUPPORTED, 0);
    return ERR_NOT_SUPPORTED;
}

void xhci_handle_root_hub_change(xhci_t* xhci) {
    volatile xhci_port_regs_t* port_regs = xhci->op_regs->port_regs;

    xprintf("xhci_handle_root_hub_change\n");

    for (uint32_t i = 0; i < xhci->rh_num_ports; i++) {
        uint32_t portsc = XHCI_READ32(&port_regs[i].portsc);
        uint32_t speed = (portsc & XHCI_MASK(PORTSC_SPEED_START, PORTSC_SPEED_BITS)) >> PORTSC_SPEED_START;

        uint32_t status_bits = portsc & PORTSC_STATUS_BITS;
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
                xprintf("port %d PORTSC_CSC connected: %d\n", i, connected);
                if (connected) {
                     status->wPortStatus |= USB_PORT_CONNECTION;
               } else {
                    if (status->wPortStatus & USB_PORT_ENABLE) {
                        status->wPortChange |= USB_PORT_ENABLE;
                    }
                    status->wPortStatus = 0;
                }
                status->wPortChange |= USB_PORT_CONNECTION;
            }
            if (portsc & PORTSC_PRC) {
                // port reset change
                xprintf("port %d PORTSC_PRC enabled: %d\n", i, enabled);
                if (enabled) {
                    status->wPortStatus &= ~USB_PORT_RESET;
                    status->wPortChange |= USB_PORT_RESET;

                    if (speed == USB_SPEED_LOW) {
                        status->wPortStatus |= USB_PORT_LOW_SPEED;
                    } else if (speed == USB_SPEED_HIGH) {
                        status->wPortStatus |= USB_PORT_HIGH_SPEED;
                    }
                }
            }

            if (status->wPortChange) {
                iotxn_t* txn = list_remove_head_type(&rh->pending_intr_reqs, iotxn_t, node);
                if (txn) {
                    xhci_rh_handle_intr_req(rh, txn);
                }
            }
        }
    }
}
