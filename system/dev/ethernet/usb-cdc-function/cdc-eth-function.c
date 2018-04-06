// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/ethernet.h>
#include <ddk/protocol/usb-function.h>
#include <ddk/usb-request.h>
#include <inet6/inet6.h>
#include <zircon/listnode.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/device/usb-device.h>
#include <zircon/hw/usb-cdc.h>

#define BULK_REQ_SIZE   2048
#define BULK_TX_COUNT   16
#define BULK_RX_COUNT   16

#define BULK_MAX_PACKET     512 // FIXME(voydanoff) USB 3.0 support
#define INTR_MAX_PACKET     sizeof(usb_cdc_speed_change_notification_t)

#define CDC_BITRATE         1000000000  // say we are gigabit

typedef struct {
    zx_device_t* zxdev;
    usb_function_protocol_t function;

    list_node_t bulk_out_reqs;      // list of usb_request_t
    list_node_t bulk_in_reqs;       // list of usb_request_t
    list_node_t tx_pending_infos;   // list of ethmac_netbuf_t
    bool unbound;                   // set to true when device is going away. Guarded by tx_mutex

    // Device attributes
    uint8_t mac_addr[ETH_MAC_SIZE];

    mtx_t ethmac_mutex;
    ethmac_ifc_t* ethmac_ifc;
    void* ethmac_cookie;
    bool online;

    mtx_t tx_mutex;
    mtx_t rx_mutex;

    uint8_t bulk_out_addr;
    uint8_t bulk_in_addr;
    uint8_t intr_addr;
    uint16_t bulk_max_packet;
} usb_cdc_t;

 static struct {
    usb_interface_descriptor_t comm_intf;
    usb_cs_header_interface_descriptor_t cdc_header;
    usb_cs_union_interface_descriptor_1_t cdc_union;
    usb_cs_ethernet_interface_descriptor_t cdc_eth;
    usb_endpoint_descriptor_t intr_ep;
    usb_interface_descriptor_t cdc_intf_0;
    usb_interface_descriptor_t cdc_intf_1;
    usb_endpoint_descriptor_t bulk_out_ep;
    usb_endpoint_descriptor_t bulk_in_ep;
} descriptors = {
    .comm_intf = {
        .bLength = sizeof(usb_interface_descriptor_t),
        .bDescriptorType = USB_DT_INTERFACE,
//      .bInterfaceNumber set later
        .bAlternateSetting = 0,
        .bNumEndpoints = 1,
        .bInterfaceClass = USB_CLASS_COMM,
        .bInterfaceSubClass = USB_CDC_SUBCLASS_ETHERNET,
        .bInterfaceProtocol = 0,
        .iInterface = 0,
    },
    .cdc_header = {
        .bLength = sizeof(usb_cs_header_interface_descriptor_t),
        .bDescriptorType = USB_DT_CS_INTERFACE,
        .bDescriptorSubType = USB_CDC_DST_HEADER,
        .bcdCDC = 0x120,
    },
    .cdc_union = {
        .bLength = sizeof(usb_cs_union_interface_descriptor_1_t),
        .bDescriptorType = USB_DT_CS_INTERFACE,
        .bDescriptorSubType = USB_CDC_DST_UNION,
//        .bControlInterface set later
//        .bSubordinateInterface set later
    },
    .cdc_eth = {
        .bLength = sizeof(usb_cs_ethernet_interface_descriptor_t),
        .bDescriptorType = USB_DT_CS_INTERFACE,
        .bDescriptorSubType = USB_CDC_DST_ETHERNET,
//        .iMACAddress filled in later
        .bmEthernetStatistics = 0,
        .wMaxSegmentSize = ETH_MTU,
        .wNumberMCFilters = 0,
        .bNumberPowerFilters = 0,
    },
    .intr_ep = {
        .bLength = sizeof(usb_endpoint_descriptor_t),
        .bDescriptorType = USB_DT_ENDPOINT,
//      .bEndpointAddress set later
        .bmAttributes = USB_ENDPOINT_INTERRUPT,
        .wMaxPacketSize = htole16(INTR_MAX_PACKET),
        .bInterval = 8,
    },
    .cdc_intf_0 = {
        .bLength = sizeof(usb_interface_descriptor_t),
        .bDescriptorType = USB_DT_INTERFACE,
//      .bInterfaceNumber set later
        .bAlternateSetting = 0,
        .bNumEndpoints = 0,
        .bInterfaceClass = USB_CLASS_CDC,
        .bInterfaceSubClass = 0,
        .bInterfaceProtocol = 0,
        .iInterface = 0,
    },
    .cdc_intf_1 = {
        .bLength = sizeof(usb_interface_descriptor_t),
        .bDescriptorType = USB_DT_INTERFACE,
//      .bInterfaceNumber set later
        .bAlternateSetting = 1,
        .bNumEndpoints = 2,
        .bInterfaceClass = USB_CLASS_CDC,
        .bInterfaceSubClass = 0,
        .bInterfaceProtocol = 0,
        .iInterface = 0,
    },
    .bulk_out_ep = {
        .bLength = sizeof(usb_endpoint_descriptor_t),
        .bDescriptorType = USB_DT_ENDPOINT,
//      .bEndpointAddress set later
        .bmAttributes = USB_ENDPOINT_BULK,
        .wMaxPacketSize = htole16(BULK_MAX_PACKET),
        .bInterval = 0,
    },
    .bulk_in_ep = {
        .bLength = sizeof(usb_endpoint_descriptor_t),
        .bDescriptorType = USB_DT_ENDPOINT,
//      .bEndpointAddress set later
        .bmAttributes = USB_ENDPOINT_BULK,
        .wMaxPacketSize = htole16(BULK_MAX_PACKET),
        .bInterval = 0,
    },
};

static zx_status_t cdc_generate_mac_address(usb_cdc_t* cdc) {
    size_t actual;
    zx_status_t status = zx_cprng_draw(cdc->mac_addr, sizeof(cdc->mac_addr), &actual);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: cdc_generate_mac_address: zx_cprng_draw failed\n", __FUNCTION__);
        return status;
    }

    // set most significant byte so we are using a locally managed address
    // TODO(voydanoff) add a way to configure a real MAC address here
    cdc->mac_addr[0] = 0x02;
    char buffer[sizeof(cdc->mac_addr) * 3];
    snprintf(buffer, sizeof(buffer), "%02X%02X%02X%02X%02X%02X",
             cdc->mac_addr[0], cdc->mac_addr[1], cdc->mac_addr[2],
             cdc->mac_addr[3], cdc->mac_addr[4], cdc->mac_addr[5]);

    return usb_function_alloc_string_desc(&cdc->function, buffer, &descriptors.cdc_eth.iMACAddress);
}

static zx_status_t cdc_ethmac_query(void* ctx, uint32_t options, ethmac_info_t* info) {
    zxlogf(TRACE, "%s:\n", __FUNCTION__);
    usb_cdc_t* cdc = ctx;

    // No options are supported
    if (options) {
        zxlogf(ERROR, "%s: unexpected options (0x%"PRIx32") to ethmac_query\n", __FUNCTION__,
                options);
        return ZX_ERR_INVALID_ARGS;
    }

    memset(info, 0, sizeof(*info));
    info->mtu = ETH_MTU;
    memcpy(info->mac, cdc->mac_addr, sizeof(cdc->mac_addr));

    return ZX_OK;
}

static void cdc_ethmac_stop(void* cookie) {
    zxlogf(TRACE, "%s:\n", __FUNCTION__);
    usb_cdc_t* cdc = cookie;
    mtx_lock(&cdc->ethmac_mutex);
    cdc->ethmac_ifc = NULL;
    mtx_unlock(&cdc->ethmac_mutex);
}

static zx_status_t cdc_ethmac_start(void* ctx_cookie, ethmac_ifc_t* ifc, void* ethmac_cookie) {
    zxlogf(TRACE, "%s:\n", __FUNCTION__);
    usb_cdc_t* cdc = ctx_cookie;
    zx_status_t status = ZX_OK;

    mtx_lock(&cdc->ethmac_mutex);
    if (cdc->ethmac_ifc) {
        status = ZX_ERR_ALREADY_BOUND;
    } else {
        cdc->ethmac_ifc = ifc;
        cdc->ethmac_cookie = ethmac_cookie;
        cdc->ethmac_ifc->status(ethmac_cookie, cdc->online ? ETH_STATUS_ONLINE : 0);
    }
    mtx_unlock(&cdc->ethmac_mutex);

    return status;
}

static zx_status_t cdc_send_locked(usb_cdc_t* cdc, ethmac_netbuf_t* netbuf) {
    uint8_t* byte_data = netbuf->data;
    size_t length = netbuf->len;

    // Make sure that we can get all of the tx buffers we need to use
    usb_request_t* tx_req = list_remove_head_type(&cdc->bulk_in_reqs, usb_request_t, node);
    if (tx_req == NULL) {
        return ZX_ERR_SHOULD_WAIT;
    }

    // Send data
    tx_req->header.length = length;
    ssize_t bytes_copied = usb_request_copyto(tx_req, byte_data, tx_req->header.length, 0);
    if (bytes_copied < 0) {
        zxlogf(LERROR, "%s: failed to copy data into send req (error %zd)\n", __FUNCTION__,
                bytes_copied);
        list_add_tail(&cdc->bulk_in_reqs, &tx_req->node);
        return ZX_ERR_INTERNAL;
    }

    usb_function_queue(&cdc->function, tx_req);

    return ZX_OK;
}

static zx_status_t cdc_ethmac_queue_tx(void* cookie, uint32_t options, ethmac_netbuf_t* netbuf) {
    usb_cdc_t* cdc = cookie;
    size_t length = netbuf->len;
    zx_status_t status;

    if (!cdc->online || length > ETH_MTU || length == 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    zxlogf(LTRACE, "%s: sending %zu bytes\n", __FUNCTION__, length);

    mtx_lock(&cdc->tx_mutex);
    if (cdc->unbound) {
        status = ZX_ERR_IO_NOT_PRESENT;
    } else {
        status = cdc_send_locked(cdc, netbuf);
        if (status == ZX_ERR_SHOULD_WAIT) {
            // No buffers available, queue it up
            list_add_tail(&cdc->tx_pending_infos, &netbuf->node);
        }
    }

    mtx_unlock(&cdc->tx_mutex);
    return status;
}

static zx_status_t ethmac_set_param(void *cookie, uint32_t param, int32_t value, void* data) {
    return ZX_ERR_NOT_SUPPORTED;
}

static ethmac_protocol_ops_t ethmac_ops = {
    .query = cdc_ethmac_query,
    .stop = cdc_ethmac_stop,
    .start = cdc_ethmac_start,
    .queue_tx = cdc_ethmac_queue_tx,
    .set_param = ethmac_set_param,
};

static void cdc_intr_complete(usb_request_t* req, void* cookie) {
    zxlogf(TRACE, "%s %d %ld\n", __FUNCTION__, req->response.status, req->response.actual);
    usb_request_release(req);
}

static zx_status_t cdc_alloc_interrupt_req(usb_cdc_t* cdc, usb_request_t** out_req) {
    usb_request_t* req;
    zx_status_t status = usb_function_req_alloc(&cdc->function, &req, INTR_MAX_PACKET,
                                                cdc->intr_addr);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: usb_request_alloc failed %d\n", __FUNCTION__, status);
        return status;
    }
    req->complete_cb = cdc_intr_complete;
    req->cookie = cdc;
    *out_req = req;
    return ZX_OK;
}

// sends network connection and speed change notifications on the interrupt endpoint
// we only do this once per USB connect, so instead of pooling usb requests we just allocate
// them here and release them when they complete.
static zx_status_t cdc_send_notifications(usb_cdc_t* cdc) {
    usb_request_t* req;
    zx_status_t status;

    usb_cdc_notification_t network_notification = {
        .bmRequestType = USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
        .bNotification = USB_CDC_NC_NETWORK_CONNECTION,
        .wValue = 1, // online
        .wIndex = descriptors.cdc_intf_0.bInterfaceNumber,
        .wLength = 0,
    };

    usb_cdc_speed_change_notification_t speed_notification = {
        .notification = {
            .bmRequestType = USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
            .bNotification = USB_CDC_NC_CONNECTION_SPEED_CHANGE,
            .wValue = 0,
            .wIndex = descriptors.cdc_intf_0.bInterfaceNumber,
            .wLength = 0,
        },
        .downlink_br = CDC_BITRATE,
        .uplink_br = CDC_BITRATE,
    };

    status = cdc_alloc_interrupt_req(cdc, &req);
    if (status != ZX_OK) return status;
    usb_request_copyto(req, &network_notification, sizeof(network_notification), 0);
    req->header.length = sizeof(network_notification);
    usb_function_queue(&cdc->function, req);

    status = cdc_alloc_interrupt_req(cdc, &req);
    if (status != ZX_OK) return status;
    usb_request_copyto(req, &speed_notification, sizeof(speed_notification), 0);
    req->header.length = sizeof(speed_notification);
    usb_function_queue(&cdc->function, req);

    return ZX_OK;
}

static void cdc_rx_complete(usb_request_t* req, void* cookie) {
    usb_cdc_t* cdc = cookie;

    zxlogf(LTRACE, "%s %d %ld\n", __FUNCTION__, req->response.status, req->response.actual);

    if (req->response.status == ZX_ERR_IO_NOT_PRESENT) {
        mtx_lock(&cdc->rx_mutex);
        list_add_head(&cdc->bulk_out_reqs, &req->node);
        mtx_unlock(&cdc->rx_mutex);
        return;
    }
    if (req->response.status != ZX_OK) {
        zxlogf(ERROR, "%s: usb_read_complete called with status %d\n",
                __FUNCTION__, req->response.status);
    }

    if (req->response.status == ZX_OK) {
        mtx_lock(&cdc->ethmac_mutex);
        if (cdc->ethmac_ifc) {
            uint8_t* data = NULL;
            usb_request_mmap(req, (void*)&data);
            cdc->ethmac_ifc->recv(cdc->ethmac_cookie, data, req->response.actual, 0);
        }
        mtx_unlock(&cdc->ethmac_mutex);
    }

    usb_function_queue(&cdc->function, req);
}

static void cdc_tx_complete(usb_request_t* req, void* cookie) {
    usb_cdc_t* cdc = cookie;

    zxlogf(LTRACE, "%s %d %ld\n", __FUNCTION__, req->response.status, req->response.actual);

    mtx_lock(&cdc->tx_mutex);
    list_add_tail(&cdc->bulk_in_reqs, &req->node);

    bool additional_tx_queued = false;
    ethmac_netbuf_t* netbuf;
    zx_status_t send_status = ZX_OK;
    if ((netbuf = list_peek_head_type(&cdc->tx_pending_infos, ethmac_netbuf_t, node))) {
        if ((send_status = cdc_send_locked(cdc, netbuf)) != ZX_ERR_SHOULD_WAIT) {
            list_remove_head(&cdc->tx_pending_infos);
            additional_tx_queued = true;
        }
    }
    mtx_unlock(&cdc->tx_mutex);

    if (additional_tx_queued) {
        mtx_lock(&cdc->ethmac_mutex);
        if (cdc->ethmac_ifc) {
            cdc->ethmac_ifc->complete_tx(cdc->ethmac_cookie, netbuf, send_status);
        }
        mtx_unlock(&cdc->ethmac_mutex);
    }
}

static const usb_descriptor_header_t* cdc_get_descriptors(void* ctx, size_t* out_length) {
    *out_length = sizeof(descriptors);
    return (const usb_descriptor_header_t *)&descriptors;
}

static zx_status_t cdc_control(void* ctx, const usb_setup_t* setup, void* buffer,
                               size_t length, size_t* out_actual) {
    *out_actual = 0;

    zxlogf(TRACE, "%s\n", __FUNCTION__);

    // USB_CDC_SET_ETHERNET_PACKET_FILTER is the only control request required by the spec
    if (setup->bmRequestType == (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) &&
        setup->bRequest == USB_CDC_SET_ETHERNET_PACKET_FILTER) {
        zxlogf(TRACE, "%s: USB_CDC_SET_ETHERNET_PACKET_FILTER\n", __FUNCTION__);
        // TODO(voydanoff) implement the requested packet filtering
        return ZX_OK;
    }

    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t cdc_set_configured(void* ctx, bool configured, usb_speed_t speed) {
    zxlogf(TRACE, "%s: %d %d\n", __FUNCTION__, configured, speed);
    usb_cdc_t* cdc = ctx;
    zx_status_t status;

    mtx_lock(&cdc->ethmac_mutex);
    cdc->online = false;
    if (cdc->ethmac_ifc) {
        cdc->ethmac_ifc->status(cdc->ethmac_cookie, 0);
    }
    mtx_unlock(&cdc->ethmac_mutex);

    if (configured) {
        if ((status = usb_function_config_ep(&cdc->function, &descriptors.intr_ep, NULL)) != ZX_OK) {
            zxlogf(ERROR, "%s: usb_function_config_ep failed\n", __FUNCTION__);
            return status;
        }
    } else {
        usb_function_disable_ep(&cdc->function, cdc->bulk_out_addr);
        usb_function_disable_ep(&cdc->function, cdc->bulk_in_addr);
        usb_function_disable_ep(&cdc->function, cdc->intr_addr);
    }

    return ZX_OK;
}

static zx_status_t cdc_set_interface(void* ctx, unsigned interface, unsigned alt_setting) {
    zxlogf(TRACE, "%s: %d %d\n", __FUNCTION__, interface, alt_setting);
    usb_cdc_t* cdc = ctx;
    zx_status_t status;

    if (interface != descriptors.cdc_intf_0.bInterfaceNumber || alt_setting > 1) {
        return ZX_ERR_INVALID_ARGS;
    }

    // TODO(voydanoff) fullspeed and superspeed support
    if (alt_setting) {
        if ((status = usb_function_config_ep(&cdc->function, &descriptors.bulk_out_ep, NULL))
                != ZX_OK ||
            (status = usb_function_config_ep(&cdc->function, &descriptors.bulk_in_ep, NULL))
                != ZX_OK) {
            zxlogf(ERROR, "%s: usb_function_config_ep failed\n", __FUNCTION__);
        }
    } else {
        if ((status = usb_function_disable_ep(&cdc->function, cdc->bulk_out_addr)) != ZX_OK ||
            (status = usb_function_disable_ep(&cdc->function, cdc->bulk_in_addr)) != ZX_OK) {
            zxlogf(ERROR, "%s: usb_function_disable_ep failed\n", __FUNCTION__);
        }
    }

    bool online = false;
    if (alt_setting && status == ZX_OK) {
        online = true;

        // queue our OUT reqs
        mtx_lock(&cdc->rx_mutex);
        usb_request_t* req;
        while ((req = list_remove_head_type(&cdc->bulk_out_reqs, usb_request_t, node)) != NULL) {
            usb_function_queue(&cdc->function, req);
        }
        mtx_unlock(&cdc->rx_mutex);

        // send status notifications on interrupt endpoint
        status = cdc_send_notifications(cdc);
    }

    mtx_lock(&cdc->ethmac_mutex);
    cdc->online = online;
    if (cdc->ethmac_ifc) {
        cdc->ethmac_ifc->status(cdc->ethmac_cookie, online ? ETH_STATUS_ONLINE : 0);
    }
    mtx_unlock(&cdc->ethmac_mutex);

    return status;
}

usb_function_interface_ops_t device_ops = {
    .get_descriptors = cdc_get_descriptors,
    .control = cdc_control,
    .set_configured = cdc_set_configured,
    .set_interface = cdc_set_interface,
};

static void usb_cdc_unbind(void* ctx) {
    zxlogf(TRACE, "%s\n", __FUNCTION__);
    usb_cdc_t* cdc = ctx;

    mtx_lock(&cdc->tx_mutex);
    cdc->unbound = true;
    if (cdc->ethmac_ifc) {
        ethmac_netbuf_t* netbuf;
        while ((netbuf = list_remove_head_type(&cdc->tx_pending_infos, ethmac_netbuf_t, node)) !=
               NULL) {
            cdc->ethmac_ifc->complete_tx(cdc->ethmac_cookie, netbuf, ZX_ERR_PEER_CLOSED);
        }
    }
    mtx_unlock(&cdc->tx_mutex);

    device_remove(cdc->zxdev);
}

static void usb_cdc_release(void* ctx) {
    zxlogf(TRACE, "%s\n", __FUNCTION__);
    usb_cdc_t* cdc = ctx;
    usb_request_t* req;

    while ((req = list_remove_head_type(&cdc->bulk_out_reqs, usb_request_t, node)) != NULL) {
        usb_request_release(req);
    }
    while ((req = list_remove_head_type(&cdc->bulk_in_reqs, usb_request_t, node)) != NULL) {
        usb_request_release(req);
    }
    mtx_destroy(&cdc->ethmac_mutex);
    mtx_destroy(&cdc->tx_mutex);
    mtx_destroy(&cdc->rx_mutex);
    free(cdc);
}

static zx_protocol_device_t usb_cdc_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = usb_cdc_unbind,
    .release = usb_cdc_release,
};

zx_status_t usb_cdc_bind(void* ctx, zx_device_t* parent) {
    zxlogf(INFO, "%s\n", __FUNCTION__);

    usb_cdc_t* cdc = calloc(1, sizeof(usb_cdc_t));
    if (!cdc) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_USB_FUNCTION, &cdc->function);
    if (status != ZX_OK) {
        free(cdc);
        return status;
    }

    list_initialize(&cdc->bulk_out_reqs);
    list_initialize(&cdc->bulk_in_reqs);
    list_initialize(&cdc->tx_pending_infos);
    mtx_init(&cdc->ethmac_mutex, mtx_plain);
    mtx_init(&cdc->tx_mutex, mtx_plain);
    mtx_init(&cdc->rx_mutex, mtx_plain);

    cdc->bulk_max_packet = BULK_MAX_PACKET; // FIXME(voydanoff) USB 3.0 support

    status = usb_function_alloc_interface(&cdc->function, &descriptors.comm_intf.bInterfaceNumber);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: usb_function_alloc_interface failed\n", __FUNCTION__);
        goto fail;
    }
    status = usb_function_alloc_interface(&cdc->function, &descriptors.cdc_intf_0.bInterfaceNumber);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: usb_function_alloc_interface failed\n", __FUNCTION__);
        goto fail;
    }
    descriptors.cdc_intf_1.bInterfaceNumber = descriptors.cdc_intf_0.bInterfaceNumber;
    descriptors.cdc_union.bControlInterface = descriptors.comm_intf.bInterfaceNumber;
    descriptors.cdc_union.bSubordinateInterface = descriptors.cdc_intf_0.bInterfaceNumber;

    status = usb_function_alloc_ep(&cdc->function, USB_DIR_OUT, &cdc->bulk_out_addr);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: usb_function_alloc_ep failed\n", __FUNCTION__);
        goto fail;
    }
    status = usb_function_alloc_ep(&cdc->function, USB_DIR_IN, &cdc->bulk_in_addr);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: usb_function_alloc_ep failed\n", __FUNCTION__);
        goto fail;
    }
    status = usb_function_alloc_ep(&cdc->function, USB_DIR_IN, &cdc->intr_addr);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: usb_function_alloc_ep failed\n", __FUNCTION__);
        goto fail;
    }

    descriptors.bulk_out_ep.bEndpointAddress = cdc->bulk_out_addr;
    descriptors.bulk_in_ep.bEndpointAddress = cdc->bulk_in_addr;
    descriptors.intr_ep.bEndpointAddress = cdc->intr_addr;

    status = cdc_generate_mac_address(cdc);
    if (status != ZX_OK) {
        goto fail;
    }

    // allocate bulk out usb requests
    usb_request_t* req;
    for (int i = 0; i < BULK_TX_COUNT; i++) {
        status = usb_function_req_alloc(&cdc->function, &req, BULK_REQ_SIZE, cdc->bulk_out_addr);
        if (status != ZX_OK) {
            goto fail;
        }
        req->complete_cb = cdc_rx_complete;
        req->cookie = cdc;
        list_add_head(&cdc->bulk_out_reqs, &req->node);
    }
    // allocate bulk in usb requests
    for (int i = 0; i < BULK_RX_COUNT; i++) {
        status = usb_function_req_alloc(&cdc->function, &req, BULK_REQ_SIZE, cdc->bulk_in_addr);
        if (status != ZX_OK) {
            goto fail;
        }

        // As per the CDC-ECM spec, we need to send a zero-length packet to signify the end of
        // transmission when the endpoint max packet size is a factor of the total transmission size
        req->header.send_zlp = true;

        req->complete_cb = cdc_tx_complete;
        req->cookie = cdc;
        list_add_head(&cdc->bulk_in_reqs, &req->node);
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "cdc-eth-function",
        .ctx = cdc,
        .ops = &usb_cdc_proto,
        .proto_id = ZX_PROTOCOL_ETHERNET_IMPL,
        .proto_ops = &ethmac_ops,
    };

    status = device_add(parent, &args, &cdc->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: add_device failed %d\n", __FUNCTION__, status);
        goto fail;
    }

    usb_function_interface_t intf = {
        .ops = &device_ops,
        .ctx = cdc,
    };
    usb_function_register(&cdc->function, &intf);

    return ZX_OK;

fail:
    usb_cdc_release(cdc);
    return status;
}

static zx_driver_ops_t usb_cdc_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_cdc_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(usb_cdc, usb_cdc_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB_FUNCTION),
    BI_ABORT_IF(NE, BIND_USB_CLASS, USB_CLASS_COMM),
    BI_ABORT_IF(NE, BIND_USB_SUBCLASS, USB_CDC_SUBCLASS_ETHERNET),
    BI_MATCH_IF(EQ, BIND_USB_PROTOCOL, 0),
ZIRCON_DRIVER_END(usb_cdc)
