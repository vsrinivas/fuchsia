// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/usb.h>
#include <ddk/usb/usb.h>
#include <zircon/device/midi.h>
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

static void usb_midi_sink_write_complete(usb_request_t* req, void* cookie) {
    usb_midi_sink_t* sink = (usb_midi_sink_t*)cookie;
    if (req->response.status == ZX_ERR_IO_NOT_PRESENT) {
        usb_req_release(&sink->usb, req);
        return;
    }

    // FIXME what to do with error here?
    mtx_lock(&sink->mutex);
    list_add_tail(&sink->free_write_reqs, &req->node);
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
    while ((req = list_remove_head_type(&sink->free_write_reqs, usb_request_t, node)) != NULL) {
        usb_req_release(&sink->usb, req);
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
        usb_request_t* req = containerof(node, usb_request_t, node);

        size_t message_length = get_midi_message_length(*src);
        if (message_length < 1 || message_length > length) return ZX_ERR_INVALID_ARGS;

        uint8_t buffer[4];
        buffer[0] = (src[0] & 0xF0) >> 4;
        buffer[1] = src[0];
        buffer[2] = (message_length > 1 ? src[1] : 0);
        buffer[3] = (message_length > 2 ? src[2] : 0);

        usb_req_copy_to(&sink->usb, req, buffer, 4, 0);
        req->header.length = 4;
        usb_request_queue(&sink->usb, req);

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

static zx_status_t usb_midi_sink_ioctl(void* ctx, uint32_t op, const void* in_buf,
                                       size_t in_len, void* out_buf, size_t out_len,
                                       size_t* out_actual) {
    switch (op) {
    case IOCTL_MIDI_GET_DEVICE_TYPE: {
        int* reply = out_buf;
        if (out_len < sizeof(*reply)) return ZX_ERR_BUFFER_TOO_SMALL;
        *reply = MIDI_TYPE_SINK;
        *out_actual = sizeof(*reply);
        return ZX_OK;
    }
    }

    return ZX_ERR_NOT_SUPPORTED;
}

static zx_protocol_device_t usb_midi_sink_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = usb_midi_sink_unbind,
    .release = usb_midi_sink_release,
    .open = usb_midi_sink_open,
    .close = usb_midi_sink_close,
    .write = usb_midi_sink_write,
    .ioctl = usb_midi_sink_ioctl,
};

zx_status_t usb_midi_sink_create(zx_device_t* device, usb_protocol_t* usb, int index,
                                  const usb_interface_descriptor_t* intf,
                                  const usb_endpoint_descriptor_t* ep) {
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
    for (int i = 0; i < WRITE_REQ_COUNT; i++) {
        usb_request_t* req;
        zx_status_t status = usb_req_alloc(usb, &req, usb_ep_max_packet(ep), ep->bEndpointAddress);
        if (status != ZX_OK) {
            usb_midi_sink_free(sink);
            return ZX_ERR_NO_MEMORY;
        }
        req->header.length = packet_size;
        req->complete_cb = usb_midi_sink_write_complete;
        req->cookie = sink;
        list_add_head(&sink->free_write_reqs, &req->node);
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
