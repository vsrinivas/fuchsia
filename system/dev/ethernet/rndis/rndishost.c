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
#include <zircon/listnode.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#define READ_REQ_COUNT 8
#define WRITE_REQ_COUNT 4
#define ETH_HEADER_SIZE 4

#define ETHMAC_MAX_TRANSMIT_DELAY 100
#define ETHMAC_MAX_RECV_DELAY 100
#define ETHMAC_TRANSMIT_DELAY 10
#define ETHMAC_RECV_DELAY 10
#define ETHMAC_INITIAL_TRANSMIT_DELAY 0
#define ETHMAC_INITIAL_RECV_DELAY 0

typedef struct {
    zx_device_t* zxdev;
    zx_device_t* usb_zxdev;
    usb_protocol_t usb;

    uint8_t mac_addr[6];
    uint8_t control_intf;
    uint32_t request_id;
    uint32_t mtu;

    uint8_t bulk_in_addr;
    uint8_t bulk_out_addr;
    uint8_t intr_addr;

    list_node_t free_read_reqs;
    list_node_t free_write_reqs;
    list_node_t free_intr_reqs;

    uint64_t rx_endpoint_delay;    // wait time between 2 recv requests
    uint64_t tx_endpoint_delay;    // wait time between 2 transmit requests

    // Interface to the ethernet layer.
    ethmac_ifc_t* ifc;
    void* cookie;

    mtx_t mutex;
} rndishost_t;

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

static void rndis_read_complete(usb_request_t* request, void* cookie) {
    zxlogf(TRACE, "rndis_read_complete\n");
    rndishost_t* eth = (rndishost_t*)cookie;

    if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
        usb_req_release(&eth->usb, request);
        return;
    }

    mtx_lock(&eth->mutex);
    if (request->response.status == ZX_ERR_IO_REFUSED) {
        zxlogf(TRACE, "rndis_read_complete usb_reset_endpoint\n");
        usb_reset_endpoint(&eth->usb, eth->bulk_in_addr);
    } else if (request->response.status == ZX_ERR_IO_INVALID) {
        zxlogf(TRACE, "rndis_read_complete Slowing down the requests by %d usec"
               " and resetting the recv endpoint\n", ETHMAC_RECV_DELAY);
        if (eth->rx_endpoint_delay < ETHMAC_MAX_RECV_DELAY) {
            eth->rx_endpoint_delay += ETHMAC_RECV_DELAY;
        }
        usb_reset_endpoint(&eth->usb, eth->bulk_in_addr);
    }
    if ((request->response.status == ZX_OK) && eth->ifc) {
        size_t len = request->response.actual;

        uint8_t* read_data;
        zx_status_t status = usb_req_mmap(&eth->usb, request, (void*)&read_data);
        if (status != ZX_OK) {
            printf("usb_req_mmap failed: %d\n", status);
            mtx_unlock(&eth->mutex);
            return;
        }

        eth->ifc->recv(eth->cookie, read_data, len, 0);
    }

    // TODO: Only usb_request_queue if the device is online.
    zx_nanosleep(zx_deadline_after(ZX_USEC(eth->rx_endpoint_delay)));
    usb_request_queue(&eth->usb, request);

    mtx_unlock(&eth->mutex);
}

static void rndis_write_complete(usb_request_t* request, void* cookie) {
    rndishost_t* eth = (rndishost_t*)cookie;

    if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
        zxlogf(ERROR, "rndis_write_complete zx_err_io_not_present\n");
        usb_req_release(&eth->usb, request);
        return;
    }

    mtx_lock(&eth->mutex);
    if (request->response.status == ZX_ERR_IO_REFUSED) {
        zxlogf(TRACE, "rndishost usb_reset_endpoint\n");
        usb_reset_endpoint(&eth->usb, eth->bulk_out_addr);
    } else if (request->response.status == ZX_ERR_IO_INVALID) {
        zxlogf(TRACE, "rndis_write_complete Slowing down the requests by %d usec"
               " and resetting the transmit endpoint\n", ETHMAC_TRANSMIT_DELAY);
        if (eth->tx_endpoint_delay < ETHMAC_MAX_TRANSMIT_DELAY) {
            eth->tx_endpoint_delay += ETHMAC_TRANSMIT_DELAY;
        }
        usb_reset_endpoint(&eth->usb, eth->bulk_out_addr);
    }

    list_add_tail(&eth->free_write_reqs, &request->node);
    mtx_unlock(&eth->mutex);
}

static void rndishost_free(rndishost_t* eth) {
    usb_request_t* txn;
    while ((txn = list_remove_head_type(&eth->free_read_reqs, usb_request_t, node)) != NULL) {
        usb_req_release(&eth->usb, txn);
    }
    while ((txn = list_remove_head_type(&eth->free_write_reqs, usb_request_t, node)) != NULL) {
        usb_req_release(&eth->usb, txn);
    }
    while ((txn = list_remove_head_type(&eth->free_intr_reqs, usb_request_t, node)) != NULL) {
        usb_req_release(&eth->usb, txn);
    }
    free(eth);
}

static zx_status_t rndishost_query(void* ctx, uint32_t options, ethmac_info_t* info) {
    zxlogf(TRACE, "rndishost_query\n");
    rndishost_t* eth = (rndishost_t*)ctx;

    zxlogf(DEBUG1, "options = %x\n", options);
    if (options) {
        return ZX_ERR_INVALID_ARGS;
    }

    memset(info, 0, sizeof(*info));
    info->mtu = eth->mtu;
    memcpy(info->mac, eth->mac_addr, sizeof(eth->mac_addr));

    return ZX_OK;
}

static void rndishost_stop(void* ctx) {
    rndishost_t* eth = (rndishost_t*)ctx;
    mtx_lock(&eth->mutex);
    eth->ifc = NULL;
    mtx_unlock(&eth->mutex);
}

static zx_status_t rndishost_start(void* ctx, ethmac_ifc_t* ifc, void* cookie) {
    rndishost_t* eth = (rndishost_t*)ctx;
    zx_status_t status = ZX_OK;

    mtx_lock(&eth->mutex);
    if (eth->ifc) {
        status = ZX_ERR_ALREADY_BOUND;
    } else {
        eth->ifc = ifc;
        eth->cookie = cookie;
        // TODO: Check that the device is online before sending ETH_STATUS_ONLINE.
        eth->ifc->status(eth->cookie, ETH_STATUS_ONLINE);
    }
    mtx_unlock(&eth->mutex);

    return status;
}

static zx_status_t rndishost_queue_tx(void* ctx, uint32_t options, ethmac_netbuf_t* netbuf) {
    size_t length = netbuf->len;
    rndishost_t* eth = (rndishost_t*)ctx;
    uint8_t* byte_data = netbuf->data;
    zx_status_t status = ZX_OK;

    mtx_lock(&eth->mutex);

    usb_request_t* req = list_remove_head_type(&eth->free_write_reqs, usb_request_t, node);
    if (req == NULL) {
        zxlogf(DEBUG1, "dropped a packet.\n");
        status = ZX_ERR_NO_RESOURCES;
        goto done;
    }

    // TODO: Check that length + header <= MTU

    rndis_packet_header header;
    uint8_t* header_data = (uint8_t*)&header;
    memset(header_data, 0, sizeof(rndis_packet_header));
    header.msg_type = RNDIS_PACKET_MSG;
    header.msg_length = sizeof(rndis_packet_header) + length;
    // The offset should be given from the beginning of the data_offset field.
    // So subtract 8 bytes for msg_type and msg_length.
    header.data_offset = sizeof(rndis_packet_header) - 8;
    header.data_length = length;

    usb_req_copy_to(&eth->usb, req, header_data, sizeof(rndis_packet_header), 0);
    ssize_t bytes_copied = usb_req_copy_to(&eth->usb, req, byte_data, length,
                                           sizeof(rndis_packet_header));
    req->header.length = sizeof(rndis_packet_header) + length;
    if (bytes_copied < 0) {
        printf("rndishost: failed to copy data into send txn (error %zd)\n", bytes_copied);
        list_add_tail(&eth->free_write_reqs, &req->node);
        goto done;
    }
    zx_nanosleep(zx_deadline_after(ZX_USEC(eth->tx_endpoint_delay)));
    usb_request_queue(&eth->usb, req);

done:
    mtx_unlock(&eth->mutex);
    return status;
}

static void rndishost_unbind(void* ctx) {
    rndishost_t* eth = (rndishost_t*)ctx;
    device_remove(eth->zxdev);
}

static void rndishost_release(void* ctx) {
    rndishost_t* eth = (rndishost_t*)ctx;
    rndishost_free(eth);
}

static zx_status_t rndishost_set_param(void *ctx, uint32_t param, int32_t value, void* data) {
    return ZX_ERR_NOT_SUPPORTED;
}

static ethmac_protocol_ops_t ethmac_ops = {
    .query = rndishost_query,
    .stop = rndishost_stop,
    .start = rndishost_start,
    .queue_tx = rndishost_queue_tx,
    .set_param = rndishost_set_param,
};

static zx_protocol_device_t rndishost_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = rndishost_unbind,
    .release = rndishost_release,
};

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
    eth->mtu = init_cmplt->max_xfer_size;

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

    // Enable data transfers
    memset(buf, 0, RNDIS_BUFFER_SIZE);
    rndis_set* set = buf;
    set->msg_type = RNDIS_SET_MSG;
    set->msg_length = sizeof(rndis_set) + 4; // 4 bytes for the filter
    set->oid = OID_GEN_CURRENT_PACKET_FILTER;
    set->info_buffer_length = 4;
    // Offset should begin at oid, so subtract 8 bytes for msg_type and msg_length.
    set->info_buffer_offset = sizeof(rndis_set) - 8;
    uint8_t* filter = buf + sizeof(rndis_set);
    *filter = RNDIS_PACKET_TYPE_DIRECTED |
              RNDIS_PACKET_TYPE_BROADCAST |
              RNDIS_PACKET_TYPE_ALL_MULTICAST |
              RNDIS_PACKET_TYPE_PROMISCUOUS;
    status = rndis_command(eth, buf);
    if (status < 0) {
        zxlogf(ERROR, "Couldn't set the packet filter.\n");
        goto fail;
    }

    if (!command_succeeded(buf, RNDIS_SET_CMPLT, sizeof(rndis_set_complete))) {
        zxlogf(ERROR, "rndishost set filter failed.\n");
        status = ZX_ERR_IO;
        goto fail;
    }

    free(buf);
    device_make_visible(eth->zxdev);
    return ZX_OK;

fail:
    free(buf);
    rndishost_unbind(eth);
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

    list_initialize(&eth->free_read_reqs);
    list_initialize(&eth->free_write_reqs);
    list_initialize(&eth->free_intr_reqs);

    mtx_init(&eth->mutex, mtx_plain);

    eth->usb_zxdev = device;
    eth->control_intf = control_intf;
    eth->bulk_in_addr = bulk_in_addr;
    eth->bulk_out_addr = bulk_out_addr;
    eth->intr_addr = intr_addr;
    eth->ifc = NULL;
    memcpy(&eth->usb, &usb, sizeof(eth->usb));

    for (int i = 0; i < READ_REQ_COUNT; i++) {
        usb_request_t* req;
        zx_status_t alloc_result = usb_req_alloc(&eth->usb, &req, RNDIS_BUFFER_SIZE, bulk_in_addr);
        if (alloc_result != ZX_OK) {
            status = alloc_result;
            goto fail;
        }
        req->complete_cb = rndis_read_complete;
        req->cookie = eth;
        list_add_head(&eth->free_read_reqs, &req->node);
    }
    for (int i = 0; i < WRITE_REQ_COUNT; i++) {
        usb_request_t* req;
        // TODO: Allocate based on mtu.
        zx_status_t alloc_result = usb_req_alloc(&eth->usb, &req, RNDIS_BUFFER_SIZE, bulk_out_addr);
        if (alloc_result != ZX_OK) {
            status = alloc_result;
            goto fail;
        }
        req->complete_cb = rndis_write_complete;
        req->cookie = eth;
        list_add_head(&eth->free_write_reqs, &req->node);
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "rndishost",
        .ctx = eth,
        .ops = &rndishost_device_proto,
        .proto_id = ZX_PROTOCOL_ETHERNET_IMPL,
        .proto_ops = &ethmac_ops,
        .flags = DEVICE_ADD_INVISIBLE,
    };

    status = device_add(eth->usb_zxdev, &args, &eth->zxdev);
    if (status < 0) {
        zxlogf(ERROR, "rndishost: failed to create device: %d\n", status);
        goto fail;
    }

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
