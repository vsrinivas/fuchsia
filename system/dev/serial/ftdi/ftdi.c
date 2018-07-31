// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/serial.h>
#include <ddk/protocol/usb.h>
#include <ddk/usb/usb.h>
#include <zircon/listnode.h>
#include <zircon/hw/usb.h>

#include <inttypes.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "ftdi.h"

#define FTDI_STATUS_SIZE 2
#define FTDI_RX_HEADER_SIZE 4

#define READ_REQ_COUNT 8
#define WRITE_REQ_COUNT 4
#define INTR_REQ_COUNT 4
#define USB_BUF_SIZE 2048
#define INTR_REQ_SIZE 4

#define FIFOSIZE 256
#define FIFOMASK (FIFOSIZE - 1)

typedef struct {
    zx_device_t* usb_device;
    zx_device_t* zxdev;
    usb_protocol_t usb;

    uint16_t ftditype;
    uint32_t baudrate;

    serial_port_info_t serial_port_info;
    serial_impl_protocol_t serial;

    serial_notify_cb notify_cb;
    void* notify_cb_cookie;
    bool enabled;
    uint32_t state;
    // pool of free USB requests
    list_node_t free_read_reqs;
    list_node_t free_write_reqs;
    // list of received packets not yet read by upper layer
    list_node_t completed_reads;
    size_t read_offset;
    mtx_t mutex;
} ftdi_t;


static uint32_t ftdi_check_state(ftdi_t* ftdi) {
    uint32_t state = 0;

    state |= list_is_empty(&ftdi->free_write_reqs) ? 0 : SERIAL_STATE_WRITABLE;

    state |= list_is_empty(&ftdi->completed_reads) ? 0 : SERIAL_STATE_READABLE;

    if (state != ftdi->state) {
        ftdi->state = state;
        if (ftdi->notify_cb) {
            ftdi->notify_cb(state, ftdi->notify_cb_cookie);
        }
    }
    return state;
}

static void ftdi_read_complete(usb_request_t* request, void* cookie) {
    ftdi_t* ftdi = (ftdi_t*)cookie;

    if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
        zxlogf(INFO,"FTDI: remote closed\n");
        usb_req_release(&ftdi->usb, request);
        return;
    }

    mtx_lock(&ftdi->mutex);
    if ((request->response.status == ZX_OK) && (request->response.actual > 2)) {
        list_add_tail(&ftdi->completed_reads, &request->node);
        ftdi_check_state(ftdi);
    } else {
        usb_request_queue(&ftdi->usb, request);
    }
    mtx_unlock(&ftdi->mutex);
}

static void ftdi_write_complete(usb_request_t* request, void* cookie) {
    ftdi_t* ftdi = (ftdi_t*)cookie;

    if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
        usb_req_release(&ftdi->usb, request);
        return;
    }
    mtx_lock(&ftdi->mutex);
    list_add_tail(&ftdi->free_write_reqs, &request->node);
    ftdi_check_state(ftdi);
    mtx_unlock(&ftdi->mutex);
}

static zx_status_t ftdi_calc_dividers(uint32_t* baudrate, uint32_t clock,       uint32_t divisor,
                                                          uint16_t* integer_div, uint16_t* fraction_div) {

    static const uint8_t frac_lookup[8] = {0, 3, 2, 4, 1, 5, 6, 7};

    uint32_t base_clock = clock/divisor;

    // integer dividers of 1 and 0 are special cases.  0=base_clock and 1 = 2/3 of base clock
    if (*baudrate >=  base_clock) {  // return with max baud rate achievable
        *fraction_div = 0;
        *integer_div = 0;
        *baudrate = base_clock;
    }
    else if (*baudrate >=  (base_clock* 2 )/3) {
        *integer_div = 1;
        *fraction_div = 0;
        *baudrate = (base_clock * 2)/3;
    } else {
        // create a 28.4 fractional integer
        uint32_t ratio = (base_clock * 16) / *baudrate;
        ratio++;    //round up if needed
        ratio = ratio & 0xfffffffe;

        *baudrate = (base_clock << 4) / ratio;
        *integer_div = ratio >> 4;
        *fraction_div = frac_lookup[ (ratio >> 1) & 0x07 ];
    }
    return ZX_OK;
}

static zx_status_t ftdi_write(void *ctx, const void* buf, size_t length, size_t* actual) {
    ftdi_t* ftdi = ctx;
    usb_request_t* req = NULL;
    zx_status_t status = ZX_OK;

    mtx_lock(&ftdi->mutex);

    req = list_remove_head_type(&ftdi->free_write_reqs, usb_request_t, node);
    if (!req) {
        status = ZX_ERR_SHOULD_WAIT;
        *actual = 0;
        goto out;
    }

    *actual = usb_req_copy_to(&ftdi->usb, req, buf, length, 0);
    req->header.length = length;

    usb_request_queue(&ftdi->usb,req);
    ftdi_check_state(ftdi);

out:
    mtx_unlock(&ftdi->mutex);
    return status;
}


static zx_status_t ftdi_read(void* ctx, void* data, size_t len, size_t* actual) {
    ftdi_t* ftdi = ctx;
    size_t bytes_copied = 0;
    size_t offset = ftdi->read_offset;
    uint8_t* buffer = (uint8_t*)data;

    mtx_lock(&ftdi->mutex);

    usb_request_t* req = list_peek_head_type(&ftdi->completed_reads,
                                              usb_request_t, node);
    while ((req) && (bytes_copied < len)) {

        size_t to_copy = req->response.actual - offset - FTDI_STATUS_SIZE;

        if ( (to_copy + bytes_copied) > len) {
            to_copy = len - bytes_copied;
        }

        usb_req_copy_from(&ftdi->usb, req, &buffer[bytes_copied],
                          to_copy, offset + FTDI_STATUS_SIZE);

        bytes_copied = bytes_copied + to_copy;

        if ((to_copy + offset + FTDI_STATUS_SIZE) < req->response.actual) {
            offset = offset + to_copy;
            goto out;
        } else {
            list_remove_head(&ftdi->completed_reads);
            // requeue the read request
            usb_request_queue(&ftdi->usb, req);
            offset = 0;
        }

        req = list_peek_head_type(&ftdi->completed_reads, usb_request_t, node);
    }
    ftdi_check_state(ftdi);

out:
    ftdi->read_offset = offset;
    mtx_unlock(&ftdi->mutex);
    *actual = bytes_copied;
    return *actual? ZX_OK : ZX_ERR_SHOULD_WAIT;
}


static zx_status_t ftdi_set_baudrate(ftdi_t* ftdi, uint32_t baudrate){
    uint16_t whole,fraction,value,index;
    zx_status_t status;

    if (ftdi == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }

    switch(ftdi->ftditype) {
        case FTDI_TYPE_R:
        case FTDI_TYPE_2232C:
        case FTDI_TYPE_BM:
            ftdi_calc_dividers(&baudrate,FTDI_C_CLK,16,&whole,&fraction);
            ftdi->baudrate = baudrate;
            break;
        default:
            return ZX_ERR_INVALID_ARGS;
    }
    value = (whole & 0x3fff) | (fraction << 14);
    index = fraction >> 2;
    status = usb_control(&ftdi->usb, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                         FTDI_SIO_SET_BAUDRATE, value, index, NULL, 0,
                         ZX_TIME_INFINITE,NULL);
    if (status == ZX_OK) {
        ftdi->baudrate = baudrate;
    }
    return status;
}

static zx_status_t ftdi_reset(ftdi_t* ftdi) {

    if (ftdi == NULL || ftdi->usb_device == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }

    return usb_control(
            &ftdi->usb,
            USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
            FTDI_SIO_RESET_REQUEST,
            FTDI_SIO_RESET, //value
            0, //index
            NULL, 0, //data,length
            ZX_TIME_INFINITE,
            NULL);
}


static zx_status_t ftdi_serial_config(void* ctx, uint32_t baud_rate, uint32_t flags) {
    ftdi_t* ftdi = ctx;

    if (baud_rate != ftdi->baudrate) {
        return ftdi_set_baudrate(ftdi, baud_rate);
    }

    return ZX_OK;
}


static zx_status_t ftdi_serial_get_info(void* ctx, serial_port_info_t* info) {
    ftdi_t* ftdi = ctx;
    memcpy(info, &ftdi->serial_port_info, sizeof(*info));
    return ZX_OK;
}

static zx_status_t ftdi_serial_enable(void* ctx, bool enable) {
    ftdi_t* ftdi = ctx;
    ftdi->enabled = enable;
    return ZX_OK;
}

static zx_status_t ftdi_set_notify_callback(void* ctx, serial_notify_cb cb, void* cookie) {
    ftdi_t* ftdi = ctx;

    if (ftdi->enabled) {
        return ZX_ERR_BAD_STATE;
    }

    ftdi->notify_cb = cb;
    ftdi->notify_cb_cookie = cookie;

    mtx_lock(&ftdi->mutex);
    ftdi_check_state(ftdi);
    mtx_unlock(&ftdi->mutex);

    return ZX_OK;
}

static serial_impl_ops_t ftdi_serial_ops = {
    .get_info = ftdi_serial_get_info,
    .config = ftdi_serial_config,
    .enable = ftdi_serial_enable,

    .read = ftdi_read,
    .write = ftdi_write,
    .set_notify_callback = ftdi_set_notify_callback,
};


static void ftdi_free(ftdi_t* ftdi) {
    usb_request_t* req;
    while ((req = list_remove_head_type(&ftdi->free_read_reqs, usb_request_t, node)) != NULL) {
        usb_req_release(&ftdi->usb, req);
    }
    while ((req = list_remove_head_type(&ftdi->free_write_reqs, usb_request_t, node)) != NULL) {
        usb_req_release(&ftdi->usb, req);
    }
    while ((req = list_remove_head_type(&ftdi->completed_reads, usb_request_t, node)) != NULL) {
        usb_req_release(&ftdi->usb, req);
    }

    free(ftdi);
}

static void ftdi_uart_release(void* ctx) {
    zxlogf(INFO,"releasing ftdi uart driver\n");
    ftdi_t* ftdi = ctx;
    ftdi_free(ftdi);
}

static void ftdi_unbind(void* ctx) {
    ftdi_t* ftdi = ctx;
    device_remove(ftdi->usb_device);
}

static zx_protocol_device_t ftdi_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = ftdi_unbind,
    .release = ftdi_uart_release,
};

static zx_status_t ftdi_bind(void* ctx, zx_device_t* device) {

    usb_protocol_t usb;
    zx_status_t status = device_get_protocol(device, ZX_PROTOCOL_USB, &usb);
    if (status != ZX_OK) {
        return status;
    }

    // find our endpoints
    usb_desc_iter_t iter;
    status = usb_desc_iter_init(&usb, &iter);

    if (status != ZX_OK) {
        return status;
    }

    usb_desc_iter_next_interface(&iter, true);

    uint8_t bulk_in_addr = 0;
    uint8_t bulk_out_addr = 0;

    usb_endpoint_descriptor_t* endp = usb_desc_iter_next_endpoint(&iter);
    //int idx = 0;
    while (endp) {
        if (usb_ep_direction(endp) == USB_ENDPOINT_OUT) {
            if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
                bulk_out_addr = endp->bEndpointAddress;
            }
        } else {
            if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
                bulk_in_addr = endp->bEndpointAddress;
            }
        }
        endp = usb_desc_iter_next_endpoint(&iter);
    }

    usb_desc_iter_release(&iter);

    if (!bulk_in_addr || !bulk_out_addr ) {
        zxlogf(ERROR,"FTDI: could not find all endpoints\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    ftdi_t* ftdi = calloc(1, sizeof(ftdi_t));
    if (!ftdi) {
        zxlogf(ERROR,"FTDI: Not enough memory\n");
        return ZX_ERR_NO_MEMORY;
    }

    ftdi->ftditype = FTDI_TYPE_R;

    list_initialize(&ftdi->free_read_reqs);
    list_initialize(&ftdi->free_write_reqs);
    list_initialize(&ftdi->completed_reads);

    ftdi->usb_device = device;

    memcpy(&ftdi->usb, &usb, sizeof(ftdi->usb));

    mtx_init(&ftdi->mutex, mtx_plain);

    for (int i = 0; i < READ_REQ_COUNT; i++) {
        usb_request_t* req;
        status = usb_req_alloc(&ftdi->usb, &req, USB_BUF_SIZE, bulk_in_addr);
        if (status != ZX_OK) {
            goto fail;
        }
        req->complete_cb = ftdi_read_complete;
        req->cookie = ftdi;
        list_add_head(&ftdi->free_read_reqs, &req->node);
    }
    for (int i = 0; i < WRITE_REQ_COUNT; i++) {
        usb_request_t* req;
        status = usb_req_alloc(&ftdi->usb, &req, USB_BUF_SIZE, bulk_out_addr);
        if (status != ZX_OK) {
            goto fail;
        }
        req->complete_cb = ftdi_write_complete;
        req->cookie = ftdi;
        list_add_head(&ftdi->free_write_reqs, &req->node);
    }

    if (ftdi_reset(ftdi) < 0) {
        zxlogf(ERROR,"FTDI reset failed\n");
        goto fail;
    }

    status = ftdi_set_baudrate(ftdi, 115200);
    if (status != ZX_OK) {
        zxlogf(ERROR,"FTDI: set baudrate failed\n");
        goto fail;
    }

    ftdi->serial_port_info.serial_class = SERIAL_CLASS_GENERIC;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "ftdi-uart",
        .ctx = ftdi,
        .ops = &ftdi_device_proto,
        .proto_id = ZX_PROTOCOL_SERIAL_IMPL,
        .proto_ops = &ftdi_serial_ops,
    };

    status = device_add(ftdi->usb_device, &args, &ftdi->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ftdi_uart: device_add failed\n");
        goto fail;
    }

    //Queue the read requests
    usb_request_t* req;
    usb_request_t* prev;
    list_for_every_entry_safe (&ftdi->free_read_reqs, req, prev, usb_request_t, node) {
        list_delete(&req->node);
        usb_request_queue(&ftdi->usb, req);
    }

    zxlogf(INFO,"ftdi bind successful\n");
    return status;

fail:
    zxlogf(ERROR,"ftdi_bind failed: %d\n", status);
    ftdi_free(ftdi);
    return status;
}

static zx_driver_ops_t _ftdi_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = ftdi_bind,
};

ZIRCON_DRIVER_BEGIN(ftdi, _ftdi_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB),
    BI_MATCH_IF(EQ, BIND_USB_VID, FTDI_VID),
    BI_MATCH_IF(EQ, BIND_USB_PID, FTDI_232R_PID),
ZIRCON_DRIVER_END(ftdi)
