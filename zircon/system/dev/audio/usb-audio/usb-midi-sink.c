// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/usb.h>
#include <fuchsia/hardware/midi/c/fidl.h>
#include <usb/usb.h>
#include <usb/usb-request.h>
#include <lib/sync/completion.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#include "midi.h"
#include "usb-audio.h"

#define WRITE_REQ_COUNT 20

typedef struct {
    zx_device_t* mxdev;
    zx_device_t* usb_mxdev;
    usb_protocol_t usb;

    // pool of free USB requests
    list_node_t free_write_reqs;
    // mutex for synchronizing access to free_write_reqs and open
    mtx_t mutex;
    // completion signals free_write_reqs not empty
    sync_completion_t free_write_completion;

    bool open;
    bool dead;

    // the last signals we reported
    zx_signals_t signals;
    uint64_t parent_req_size;
} usb_midi_sink_t;

static void update_signals(usb_midi_sink_t* sink) {
    zx_signals_t new_signals = 0;
    if (sink->dead) {
        new_signals |= (DEV_STATE_WRITABLE | DEV_STATE_ERROR);
    } else if (!list_is_empty(&sink->free_write_reqs)) {
        new_signals |= DEV_STATE_WRITABLE;
    }
    if (new_signals != sink->signals) {
        device_state_clr_set(sink->mxdev,
                             sink->signals & ~new_signals,
                             new_signals & ~sink->signals);
        sink->signals = new_signals;
    }
}

static void usb_midi_sink_write_complete(void* ctx, usb_request_t* req) {
    usb_midi_sink_t* sink = (usb_midi_sink_t*)ctx;
    if (req->response.status == ZX_ERR_IO_NOT_PRESENT) {
        usb_request_release(req);
        return;
    }

    // FIXME what to do with error here?
    mtx_lock(&sink->mutex);
    zx_status_t status = usb_req_list_add_tail(&sink->free_write_reqs, req, sink->parent_req_size);
    ZX_DEBUG_ASSERT(status == ZX_OK);
    sync_completion_signal(&sink->free_write_completion);
    update_signals(sink);
    mtx_unlock(&sink->mutex);
}

static void usb_midi_sink_unbind(void* ctx) {
    usb_midi_sink_t* sink = ctx;
    sink->dead = true;
    update_signals(sink);
    sync_completion_signal(&sink->free_write_completion);
    device_remove(sink->mxdev);
}

static void usb_midi_sink_free(usb_midi_sink_t* sink) {
    usb_request_t* req;
    while ((req = usb_req_list_remove_head(&sink->free_write_reqs, sink->parent_req_size))
           != NULL) {
        usb_request_release(req);
    }
    free(sink);
}

static void usb_midi_sink_release(void* ctx) {
    usb_midi_sink_t* sink = ctx;
    usb_midi_sink_free(sink);
}

static zx_status_t usb_midi_sink_open(void* ctx, zx_device_t** dev_out, uint32_t flags) {
    usb_midi_sink_t* sink = ctx;
    zx_status_t result;

    mtx_lock(&sink->mutex);
    if (sink->open) {
        result = ZX_ERR_ALREADY_BOUND;
    } else {
        sink->open = true;
        result = ZX_OK;
    }
    mtx_unlock(&sink->mutex);

    return result;
}

static zx_status_t usb_midi_sink_close(void* ctx, uint32_t flags) {
    usb_midi_sink_t* sink = ctx;

    mtx_lock(&sink->mutex);
    sink->open = false;
    mtx_unlock(&sink->mutex);

    return ZX_OK;
}

static zx_status_t usb_midi_sink_write(void* ctx, const void* data, size_t length,
                                       zx_off_t offset, size_t* actual) {
    usb_midi_sink_t* sink = ctx;

    if (sink->dead) {
        return ZX_ERR_IO_NOT_PRESENT;
    }

    zx_status_t status = ZX_OK;
    size_t out_actual = length;

    const uint8_t* src = (uint8_t *)data;

    while (length > 0) {
        sync_completion_wait(&sink->free_write_completion, ZX_TIME_INFINITE);
        if (sink->dead) {
            return ZX_ERR_IO_NOT_PRESENT;
        }
        mtx_lock(&sink->mutex);
        list_node_t* node = list_remove_head(&sink->free_write_reqs);
        if (list_is_empty(&sink->free_write_reqs)) {
            sync_completion_reset(&sink->free_write_completion);
        }
        mtx_unlock(&sink->mutex);
        if (!node) {
            // shouldn't happen!
            status = ZX_ERR_INTERNAL;
            goto out;
        }
        usb_req_internal_t* req_int = containerof(node, usb_req_internal_t, node);
        usb_request_t* req = REQ_INTERNAL_TO_USB_REQ(req_int, sink->parent_req_size);

        size_t message_length = get_midi_message_length(*src);
        if (message_length < 1 || message_length > length) return ZX_ERR_INVALID_ARGS;

        uint8_t buffer[4];
        buffer[0] = (src[0] & 0xF0) >> 4;
        buffer[1] = src[0];
        buffer[2] = (message_length > 1 ? src[1] : 0);
        buffer[3] = (message_length > 2 ? src[2] : 0);

        usb_request_copy_to(req, buffer, 4, 0);
        req->header.length = 4;
        usb_request_complete_t complete = {
            .callback = usb_midi_sink_write_complete,
            .ctx = sink,
        };
        usb_request_queue(&sink->usb, req, &complete);

        src += message_length;
        length -= message_length;
    }

out:
    update_signals(sink);
    if (status == ZX_OK) {
        *actual = out_actual;
    }
    return status;
}

static zx_status_t fidl_GetInfo(void* ctx, fidl_txn_t* txn) {
    fuchsia_hardware_midi_Info info = {};
    info.is_sink = true;
    return fuchsia_hardware_midi_DeviceGetInfo_reply(txn, &info);
}

static zx_status_t usb_midi_sink_message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
    static const fuchsia_hardware_midi_Device_ops_t ops = {
        .GetInfo = fidl_GetInfo,
    };
    return fuchsia_hardware_midi_Device_dispatch(ctx, txn, msg, &ops);
}

static zx_protocol_device_t usb_midi_sink_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = usb_midi_sink_unbind,
    .release = usb_midi_sink_release,
    .open = usb_midi_sink_open,
    .close = usb_midi_sink_close,
    .write = usb_midi_sink_write,
    .message = usb_midi_sink_message,
};

zx_status_t usb_midi_sink_create(zx_device_t* device, usb_protocol_t* usb, int index,
                                  const usb_interface_descriptor_t* intf,
                                  const usb_endpoint_descriptor_t* ep,
                                  const size_t parent_req_size) {
    usb_midi_sink_t* sink = calloc(1, sizeof(usb_midi_sink_t));
    if (!sink) {
        printf("Not enough memory for usb_midi_sink_t\n");
        return ZX_ERR_NO_MEMORY;
    }

    list_initialize(&sink->free_write_reqs);
    sink->usb_mxdev = device;
    memcpy(&sink->usb, usb, sizeof(sink->usb));

    int packet_size = usb_ep_max_packet(ep);
    if (intf->bAlternateSetting != 0) {
        usb_set_interface(usb, intf->bInterfaceNumber, intf->bAlternateSetting);
    }

    sink->parent_req_size = parent_req_size;
    for (int i = 0; i < WRITE_REQ_COUNT; i++) {
        usb_request_t* req;
        zx_status_t status = usb_request_alloc(&req, usb_ep_max_packet(ep), ep->bEndpointAddress,
                                               parent_req_size + sizeof(usb_req_internal_t));
        if (status != ZX_OK) {
            usb_midi_sink_free(sink);
            return ZX_ERR_NO_MEMORY;
        }
        req->header.length = packet_size;
        status = usb_req_list_add_head(&sink->free_write_reqs, req, parent_req_size);
        ZX_DEBUG_ASSERT(status == ZX_OK);
    }
    sync_completion_signal(&sink->free_write_completion);

    char name[ZX_DEVICE_NAME_MAX];
    snprintf(name, sizeof(name), "usb-midi-sink-%d", index);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = sink,
        .ops = &usb_midi_sink_device_proto,
        .proto_id = ZX_PROTOCOL_MIDI,
    };

    zx_status_t status = device_add(device, &args, &sink->mxdev);
    if (status != ZX_OK) {
        printf("device_add failed in usb_midi_sink_create\n");
        usb_midi_sink_free(sink);
    }

    return status;
}
