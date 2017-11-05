// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rndishost.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/ethernet.h>
#include <ddk/protocol/usb.h>
#include <driver/usb.h>
#include <zircon/hw/usb-cdc.h>
#include <zircon/hw/usb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

typedef struct {
    zx_device_t* zxdev;
    zx_device_t* usb_zxdev;
    usb_protocol_t usb;

    uint8_t mac_addr[6];
    uint8_t control_intf;
    uint32_t request_id;

    uint8_t bulk_in_addr;
    uint8_t bulk_out_addr;
    uint8_t intr_addr;
} rndishost_t;

static void rndishost_free(rndishost_t* eth) {
    free(eth);
}

static void dump_buffer(void* buf) {
    uint8_t* p = buf;
    for (int i = 0; i < RNDIS_BUFFER_SIZE; i += 4) {
        if (i != 0 && i % 24 == 0) {
            zxlogf(DEBUG1, "\n");
        }
        zxlogf(DEBUG1, "%08x ", p[i] | p[i + 1] << 8 | p[i + 2] << 16 | p[i + 3] << 24);
    }
    zxlogf(DEBUG1, "\n");
}

static bool command_succeeded(void* buf, uint32_t type, uint32_t length) {
    rndis_header_complete* header = buf;
    if (header->msg_type != type) {
        zxlogf(DEBUG1, "Bad type: Actual: %x, Expected: %x.\n",
                header->msg_type, type);
        return false;
    }
    if (header->msg_length != length) {
        zxlogf(DEBUG1, "Bad length: Actual: %d, Expected: %d.\n",
                header->msg_length, length);
        return false;
    }
    if (header->status != RNDIS_STATUS_SUCCESS) {
        zxlogf(DEBUG1, "Bad status: %x.\n", header->status);
        return false;
    }
    return true;
}

static zx_status_t rndis_command(rndishost_t* eth, void* buf) {
    rndis_header* header = buf;
    uint32_t request_id = eth->request_id++;
    header->request_id = request_id;

    zx_status_t status;
    status = usb_control(&eth->usb, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                         USB_CDC_SEND_ENCAPSULATED_COMMAND,
                         0, eth->control_intf, buf, header->msg_length, ZX_TIME_INFINITE, NULL);

    if (status < 0) {
        return status;
    }

    // TODO: Set a reasonable timeout on this call.
    status = usb_control(&eth->usb, USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                         USB_CDC_GET_ENCAPSULATED_RESPONSE,
                         0, eth->control_intf, buf, RNDIS_BUFFER_SIZE, ZX_TIME_INFINITE, NULL);

    if (header->request_id != request_id) {
        return ZX_ERR_IO_DATA_INTEGRITY;
    }

    return status;
}

static int rndis_start_thread(void* arg) {
    rndishost_t* eth = (rndishost_t*)arg;
    void* buf = malloc(RNDIS_BUFFER_SIZE);
    memset(buf, 0, RNDIS_BUFFER_SIZE);

    // Send an initialization message to the device.
    rndis_init* init = buf;
    init->msg_type = RNDIS_INITIALIZE_MSG;
    init->msg_length = sizeof(rndis_init);
    init->major_version = RNDIS_MAJOR_VERSION;
    init->minor_version = RNDIS_MINOR_VERSION;
    init->max_xfer_size = RNDIS_MAX_XFER_SIZE;

    zx_status_t status = rndis_command(eth, buf);
    if (status < 0) {
        zxlogf(DEBUG1, "rndishost bad status on initial message. %d\n", status);
        goto fail;
    }

    // TODO: Save important fields of init_cmplt.
    rndis_init_complete* init_cmplt = buf;
    if (!command_succeeded(buf, RNDIS_INITIALIZE_CMPLT, sizeof(*init_cmplt))) {
        zxlogf(DEBUG1, "rndishost initialization failed.\n");
        status = ZX_ERR_IO;
        goto fail;
    }

    // Check the PHY, this is optional and may not be supported by the device.
    uint32_t* phy;
    memset(buf, 0, RNDIS_BUFFER_SIZE);
    rndis_query* query = buf;
    query->msg_type = RNDIS_QUERY_MSG;
    query->msg_length = sizeof(rndis_query) + sizeof(*phy);
    query->oid = OID_GEN_PHYSICAL_MEDIUM;
    query->info_buffer_length = sizeof(*phy);
    query->info_buffer_offset = RNDIS_QUERY_BUFFER_OFFSET;
    status = rndis_command(eth, buf);
    if (status == ZX_OK) {
        // TODO: Do something with this information.
        rndis_query_complete* phy_query_cmplt = buf;
        if (command_succeeded(buf, RNDIS_QUERY_CMPLT, sizeof(*phy_query_cmplt) +
                              phy_query_cmplt->info_buffer_length)) {
            // The offset given in the reply is from the beginning of the request_id
            // field. So, add 8 for the msg_type and msg_length fields.
            phy = buf + 8 + phy_query_cmplt->info_buffer_offset;
        }
    }

    // Query the device for a MAC address.
    memset(buf, 0, RNDIS_BUFFER_SIZE);
    query->msg_type = RNDIS_QUERY_MSG;
    query->msg_length = sizeof(rndis_query) + 48;
    query->oid = OID_802_3_PERMANENT_ADDRESS;
    query->info_buffer_length = 48;
    query->info_buffer_offset = RNDIS_QUERY_BUFFER_OFFSET;
    status = rndis_command(eth, buf);
    if (status < 0) {
        zxlogf(ERROR, "Couldn't get device physical address\n");
        goto fail;
    }

    rndis_query_complete* mac_query_cmplt = buf;
    if (!command_succeeded(buf, RNDIS_QUERY_CMPLT, sizeof(*mac_query_cmplt) + 
                           mac_query_cmplt->info_buffer_length)) {
        zxlogf(DEBUG1, "rndishost MAC query failed.\n");
        status = ZX_ERR_IO;
        goto fail;
    }
    uint8_t* mac_addr = buf + 8 + mac_query_cmplt->info_buffer_offset;
    memcpy(eth->mac_addr, mac_addr, sizeof(eth->mac_addr));
    zxlogf(INFO, "rndishost MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n",
            eth->mac_addr[0], eth->mac_addr[1], eth->mac_addr[2],
            eth->mac_addr[3], eth->mac_addr[4], eth->mac_addr[5]);

    // TODO: Driver is WIP.
    status = ZX_ERR_NOT_SUPPORTED;
    goto fail;

    return ZX_OK;

fail:
    free(buf);
    rndishost_free(eth);
    return status;
}

static zx_status_t rndishost_bind(void* ctx, zx_device_t* device) {
    usb_protocol_t usb;

    zx_status_t status = device_get_protocol(device, ZX_PROTOCOL_USB, &usb);
    if (status != ZX_OK) {
        return status;
    }

    // Find our endpoints.
    usb_desc_iter_t iter;
    status = usb_desc_iter_init(&usb, &iter);
    if (status < 0) {
        return status;
    }

    // We should have two interfaces: the CDC classified interface the bulk in
    // and out endpoints, and the RNDIS interface for control. The RNDIS
    // interface will be classified as USB_CLASS_WIRELESS when the device is
    // used for tethering.
    // TODO: Figure out how to handle other RNDIS use cases.
    usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, false);
    uint8_t bulk_in_addr = 0;
    uint8_t bulk_out_addr = 0;
    uint8_t intr_addr = 0;
    uint8_t control_intf = 0;
    while (intf) {
        if (intf->bInterfaceClass == USB_CLASS_WIRELESS) {
            control_intf = intf->bInterfaceNumber;
            if (intf->bNumEndpoints != 1) {
                usb_desc_iter_release(&iter);
                return ZX_ERR_NOT_SUPPORTED;
            }
            usb_endpoint_descriptor_t* endp = usb_desc_iter_next_endpoint(&iter);
            while (endp) {
                if (usb_ep_direction(endp) == USB_ENDPOINT_IN &&
                    usb_ep_type(endp) == USB_ENDPOINT_INTERRUPT) {
                    intr_addr = endp->bEndpointAddress;
                }
                endp = usb_desc_iter_next_endpoint(&iter);
            }
        } else if (intf->bInterfaceClass == USB_CLASS_CDC) {
            if (intf->bNumEndpoints != 2) {
                usb_desc_iter_release(&iter);
                return ZX_ERR_NOT_SUPPORTED;
            }
            usb_endpoint_descriptor_t* endp = usb_desc_iter_next_endpoint(&iter);
            while (endp) {
                if (usb_ep_direction(endp) == USB_ENDPOINT_OUT) {
                    if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
                        bulk_out_addr = endp->bEndpointAddress;
                    }
                } else if (usb_ep_direction(endp) == USB_ENDPOINT_IN) {
                    if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
                        bulk_in_addr = endp->bEndpointAddress;
                    }
                }
                endp = usb_desc_iter_next_endpoint(&iter);
            }
        } else {
            usb_desc_iter_release(&iter);
            return ZX_ERR_NOT_SUPPORTED;
        }
        intf = usb_desc_iter_next_interface(&iter, false);
    }
    usb_desc_iter_release(&iter);

    if (!bulk_in_addr || !bulk_out_addr || !intr_addr) {
        zxlogf(ERROR, "rndishost couldn't find endpoints\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    rndishost_t* eth = calloc(1, sizeof(rndishost_t));
    if (!eth) {
        return ZX_ERR_NO_MEMORY;
    }

    eth->usb_zxdev = device;
    eth->control_intf = control_intf;
    eth->bulk_in_addr = bulk_in_addr;
    eth->bulk_out_addr = bulk_out_addr;
    eth->intr_addr = intr_addr;
    memcpy(&eth->usb, &usb, sizeof(eth->usb));

    thrd_t thread;
    int ret = thrd_create_with_name(&thread, rndis_start_thread,
                                    eth, "rndishost_start_thread");
    if (ret != thrd_success) {
        status = ZX_ERR_NO_RESOURCES;
        goto fail;
    }
    // TODO: Save the thread in rndishost_t and join when we release the device.
    thrd_detach(thread);

    return ZX_OK;

fail:
    zxlogf(ERROR, "rndishost_bind failed: %d\n", status);
    rndishost_free(eth);
    return status;
}

static zx_driver_ops_t rndis_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = rndishost_bind,
};

// TODO: Make sure we can bind to all RNDIS use cases. USB_CLASS_WIRELESS only
// covers the tethered device case.
ZIRCON_DRIVER_BEGIN(rndishost, rndis_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_CLASS, USB_CLASS_WIRELESS),
    BI_ABORT_IF(NE, BIND_USB_SUBCLASS, RNDIS_SUBCLASS),
    BI_MATCH_IF(EQ, BIND_USB_PROTOCOL, RNDIS_PROTOCOL),
ZIRCON_DRIVER_END(rndishost)
