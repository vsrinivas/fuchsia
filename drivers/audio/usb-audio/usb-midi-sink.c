// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/common/usb.h>
#include <magenta/device/midi.h>
#include <sync/completion.h>
#include <stdlib.h>
#include <stdio.h>
#include <threads.h>

#include "midi.h"
#include "usb-audio.h"

#define WRITE_REQ_COUNT 20

typedef struct {
    mx_device_t* mxdev;
    mx_device_t* usb_mxdev;

    // pool of free USB requests
    list_node_t free_write_reqs;
    // mutex for synchronizing access to free_write_reqs and open
    mtx_t mutex;
    // completion signals free_write_reqs not empty
    completion_t free_write_completion;

    bool open;
    bool dead;

    // the last signals we reported
    mx_signals_t signals;
} usb_midi_sink_t;

static void update_signals(usb_midi_sink_t* sink) {
    mx_signals_t new_signals = 0;
    if (sink->dead) {
        new_signals |= (DEV_STATE_WRITABLE | DEV_STATE_ERROR);
    } else if (!list_is_empty(&sink->free_write_reqs)) {
        new_signals |= DEV_STATE_WRITABLE;
    }
    if (new_signals != sink->signals) {
        device_state_set_clr(sink->mxdev, new_signals & ~sink->signals,
                             sink->signals & ~new_signals);
        sink->signals = new_signals;
    }
}

static void usb_midi_sink_write_complete(iotxn_t* txn, void* cookie) {
    if (txn->status == ERR_PEER_CLOSED) {
        iotxn_release(txn);
        return;
    }

    usb_midi_sink_t* sink = (usb_midi_sink_t*)cookie;
    // FIXME what to do with error here?
    mtx_lock(&sink->mutex);
    list_add_tail(&sink->free_write_reqs, &txn->node);
    completion_signal(&sink->free_write_completion);
    update_signals(sink);
    mtx_unlock(&sink->mutex);
}

static void usb_midi_sink_unbind(void* ctx) {
    usb_midi_sink_t* sink = ctx;
    sink->dead = true;
    update_signals(sink);
    completion_signal(&sink->free_write_completion);
    device_remove(sink->mxdev);
}

static void usb_midi_sink_free(usb_midi_sink_t* sink) {
    iotxn_t* txn;
    while ((txn = list_remove_head_type(&sink->free_write_reqs, iotxn_t, node)) != NULL) {
        iotxn_release(txn);
    }
    free(sink);
}

static void usb_midi_sink_release(void* ctx) {
    usb_midi_sink_t* sink = ctx;
    usb_midi_sink_free(sink);
}

static mx_status_t usb_midi_sink_open(void* ctx, mx_device_t** dev_out, uint32_t flags) {
    usb_midi_sink_t* sink = ctx;
    mx_status_t result;

    mtx_lock(&sink->mutex);
    if (sink->open) {
        result = ERR_ALREADY_BOUND;
    } else {
        sink->open = true;
        result = NO_ERROR;
    }
    mtx_unlock(&sink->mutex);

    return result;
}

static mx_status_t usb_midi_sink_close(void* ctx, uint32_t flags) {
    usb_midi_sink_t* sink = ctx;

    mtx_lock(&sink->mutex);
    sink->open = false;
    mtx_unlock(&sink->mutex);

    return NO_ERROR;
}

static mx_status_t usb_midi_sink_write(void* ctx, const void* data, size_t length,
                                       mx_off_t offset, size_t* actual) {
    usb_midi_sink_t* sink = ctx;

    if (sink->dead) {
        return ERR_PEER_CLOSED;
    }

    mx_status_t status = NO_ERROR;
    size_t out_actual = length;

    const uint8_t* src = (uint8_t *)data;

    while (length > 0) {
        completion_wait(&sink->free_write_completion, MX_TIME_INFINITE);
        if (sink->dead) {
            return ERR_PEER_CLOSED;
        }
        mtx_lock(&sink->mutex);
        list_node_t* node = list_remove_head(&sink->free_write_reqs);
        if (list_is_empty(&sink->free_write_reqs)) {
            completion_reset(&sink->free_write_completion);
        }
        mtx_unlock(&sink->mutex);
        if (!node) {
            // shouldn't happen!
            status = ERR_INTERNAL;
            goto out;
        }
        iotxn_t* txn = containerof(node, iotxn_t, node);

        size_t message_length = get_midi_message_length(*src);
        if (message_length < 1 || message_length > length) return ERR_INVALID_ARGS;

        uint8_t buffer[4];
        buffer[0] = (src[0] & 0xF0) >> 4;
        buffer[1] = src[0];
        buffer[2] = (message_length > 1 ? src[1] : 0);
        buffer[3] = (message_length > 2 ? src[2] : 0);

        iotxn_copyto(txn, buffer, 4, 0);
        txn->length = 4;
        iotxn_queue(sink->usb_mxdev, txn);

        src += message_length;
        length -= message_length;
    }

out:
    update_signals(sink);
    if (status == NO_ERROR) {
        *actual = out_actual;
    }
    return status;
}

static mx_status_t usb_midi_sink_ioctl(void* ctx, uint32_t op, const void* in_buf,
                                       size_t in_len, void* out_buf, size_t out_len,
                                       size_t* out_actual) {
    switch (op) {
    case IOCTL_MIDI_GET_DEVICE_TYPE: {
        int* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_BUFFER_TOO_SMALL;
        *reply = MIDI_TYPE_SINK;
        *out_actual = sizeof(*reply);
        return NO_ERROR;
    }
    }

    return ERR_NOT_SUPPORTED;
}

static mx_protocol_device_t usb_midi_sink_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = usb_midi_sink_unbind,
    .release = usb_midi_sink_release,
    .open = usb_midi_sink_open,
    .close = usb_midi_sink_close,
    .write = usb_midi_sink_write,
    .ioctl = usb_midi_sink_ioctl,
};

mx_status_t usb_midi_sink_create(mx_device_t* device, int index,
                                  usb_interface_descriptor_t* intf, usb_endpoint_descriptor_t* ep) {
    usb_midi_sink_t* sink = calloc(1, sizeof(usb_midi_sink_t));
    if (!sink) {
        printf("Not enough memory for usb_midi_sink_t\n");
        return ERR_NO_MEMORY;
    }

    list_initialize(&sink->free_write_reqs);
    sink->usb_mxdev = device;

    int packet_size = usb_ep_max_packet(ep);
    if (intf->bAlternateSetting != 0) {
        usb_set_interface(device, intf->bInterfaceNumber, intf->bAlternateSetting);
    }
    for (int i = 0; i < WRITE_REQ_COUNT; i++) {
        iotxn_t* txn = usb_alloc_iotxn(ep->bEndpointAddress, usb_ep_max_packet(ep));
        if (!txn) {
            usb_midi_sink_free(sink);
            return ERR_NO_MEMORY;
        }
        txn->length = packet_size;
        txn->complete_cb = usb_midi_sink_write_complete;
        txn->cookie = sink;
        list_add_head(&sink->free_write_reqs, &txn->node);
    }
    completion_signal(&sink->free_write_completion);

    char name[MX_DEVICE_NAME_MAX];
    snprintf(name, sizeof(name), "usb-midi-sink-%d\n", index);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = sink,
        .ops = &usb_midi_sink_device_proto,
        .proto_id = MX_PROTOCOL_MIDI,
    };

    mx_status_t status = device_add(device, &args, &sink->mxdev);
    if (status != NO_ERROR) {
        printf("device_add failed in usb_midi_sink_create\n");
        usb_midi_sink_free(sink);
    }

    return status;
}

