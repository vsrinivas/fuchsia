// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/completion.h>
#include <ddk/device.h>
#include <ddk/common/usb.h>
#include <magenta/device/midi.h>
#include <stdlib.h>
#include <stdio.h>
#include <threads.h>

#include "midi.h"
#include "usb-audio.h"

#define WRITE_REQ_COUNT 20

typedef struct {
    mx_device_t device;
    mx_device_t* usb_device;

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
#define get_usb_midi_sink(dev) containerof(dev, usb_midi_sink_t, device)

static void update_signals(usb_midi_sink_t* sink) {
    mx_signals_t new_signals = 0;
    if (sink->dead) {
        new_signals |= (DEV_STATE_WRITABLE | DEV_STATE_ERROR);
    } else if (!list_is_empty(&sink->free_write_reqs)) {
        new_signals |= DEV_STATE_WRITABLE;
    }
    if (new_signals != sink->signals) {
        device_state_set_clr(&sink->device, new_signals & ~sink->signals, sink->signals & ~new_signals);
        sink->signals = new_signals;
    }
}

static void usb_midi_sink_write_complete(iotxn_t* txn, void* cookie) {
    if (txn->status == ERR_REMOTE_CLOSED) {
        txn->ops->release(txn);
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

static void usb_midi_sink_unbind(mx_device_t* device) {
    usb_midi_sink_t* sink = get_usb_midi_sink(device);
    sink->dead = true;
    update_signals(sink);
    completion_signal(&sink->free_write_completion);
    device_remove(&sink->device);
}

static mx_status_t usb_midi_sink_release(mx_device_t* device) {
    usb_midi_sink_t* sink = get_usb_midi_sink(device);

    iotxn_t* txn;
    while ((txn = list_remove_head_type(&sink->free_write_reqs, iotxn_t, node)) != NULL) {
        txn->ops->release(txn);
    }
    free(sink);
    return NO_ERROR;
}

static mx_status_t usb_midi_sink_open(mx_device_t* dev, mx_device_t** dev_out, uint32_t flags) {
    usb_midi_sink_t* sink = get_usb_midi_sink(dev);
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

static mx_status_t usb_midi_sink_close(mx_device_t* dev) {
    usb_midi_sink_t* sink = get_usb_midi_sink(dev);

    mtx_lock(&sink->mutex);
    sink->open = false;
    mtx_unlock(&sink->mutex);

    return NO_ERROR;
}

static ssize_t usb_midi_sink_write(mx_device_t* dev, const void* data, size_t length, mx_off_t offset) {
    usb_midi_sink_t* sink = get_usb_midi_sink(dev);

    if (sink->dead) {
        return ERR_REMOTE_CLOSED;
    }

    mx_status_t status = length;

    const uint8_t* src = (uint8_t *)data;

    while (length > 0) {
        completion_wait(&sink->free_write_completion, MX_TIME_INFINITE);
        if (sink->dead) {
            return ERR_REMOTE_CLOSED;
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

        txn->ops->copyto(txn, buffer, 4, 0);
        txn->length = 4;
        iotxn_queue(sink->usb_device, txn);

        src += message_length;
        length -= message_length;
    }

out:
    update_signals(sink);
    return status;
}

static ssize_t usb_midi_sink_ioctl(mx_device_t* dev, uint32_t op, const void* in_buf,
                                    size_t in_len, void* out_buf, size_t out_len) {
    switch (op) {
    case IOCTL_MIDI_GET_DEVICE_TYPE: {
        int* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_BUFFER_TOO_SMALL;
        *reply = MIDI_TYPE_SINK;
        return sizeof(*reply);
    }
    }

    return ERR_NOT_SUPPORTED;
}

static mx_protocol_device_t usb_midi_sink_device_proto = {
    .unbind = usb_midi_sink_unbind,
    .release = usb_midi_sink_release,
    .open = usb_midi_sink_open,
    .close = usb_midi_sink_close,
    .write = usb_midi_sink_write,
    .ioctl = usb_midi_sink_ioctl,
};

mx_status_t usb_midi_sink_create(mx_driver_t* driver, mx_device_t* device, int index,
                                  usb_interface_descriptor_t* intf, usb_endpoint_descriptor_t* ep) {
    usb_midi_sink_t* sink = calloc(1, sizeof(usb_midi_sink_t));
    if (!sink) {
        printf("Not enough memory for usb_midi_sink_t\n");
        return ERR_NO_MEMORY;
    }

    list_initialize(&sink->free_write_reqs);

    sink->usb_device = device;

    int packet_size = usb_ep_max_packet(ep);
    if (intf->bAlternateSetting != 0) {
        usb_set_interface(device, intf->bInterfaceNumber, intf->bAlternateSetting);
    }
    for (int i = 0; i < WRITE_REQ_COUNT; i++) {
        iotxn_t* txn = usb_alloc_iotxn(ep->bEndpointAddress, usb_ep_max_packet(ep), 0);
        if (!txn)
            return ERR_NO_MEMORY;
        txn->length = packet_size;
        txn->complete_cb = usb_midi_sink_write_complete;
        txn->cookie = sink;
        list_add_head(&sink->free_write_reqs, &txn->node);
    }
    completion_signal(&sink->free_write_completion);

    char name[MX_DEVICE_NAME_MAX];
    snprintf(name, sizeof(name), "usb-midi-sink-%d\n", index);
    device_init(&sink->device, driver, name, &usb_midi_sink_device_proto);

    sink->device.protocol_id = MX_PROTOCOL_MIDI;
    sink->device.protocol_ops = NULL;
    mx_status_t status = device_add(&sink->device, sink->usb_device);
    if (status != NO_ERROR) {
        printf("device_add failed in usb_midi_sink_create\n");
        usb_midi_sink_release(&sink->device);
    }

    return status;
}

