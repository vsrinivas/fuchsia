// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/common/usb.h>
#include <magenta/device/audio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#include "usb-audio.h"

#define READ_REQ_COUNT 20

typedef struct {
    mx_device_t device;
    mx_device_t* usb_device;
    uint8_t ep_addr;
    uint8_t interface_number;
    uint8_t alternate_setting;

    // pool of free USB requests
    list_node_t free_read_reqs;
    // list of received packets not yet read by upper layer
    list_node_t completed_reads;
    int completed_read_count;
    // mutex for synchronizing access to free_read_reqs, completed_reads, open and started
    mtx_t mutex;

    bool open;
    bool started;
    bool dead;

    // supported sample rates
    uint32_t* sample_rates;
    int sample_rate_count;
    // current sample rate
    uint32_t sample_rate;
    int channels;

    // the last signals we reported
    mx_signals_t signals;
} usb_audio_source_t;
#define get_usb_audio_source(dev) containerof(dev, usb_audio_source_t, device)

static void update_signals(usb_audio_source_t* source) {
    mx_signals_t new_signals = 0;
    if (source->dead) {
        new_signals |= (DEV_STATE_READABLE | DEV_STATE_ERROR);
    } else if (source->completed_read_count > 0) {
        new_signals |= DEV_STATE_READABLE;
    }
    if (new_signals != source->signals) {
        device_state_set_clr(&source->device, new_signals & ~source->signals, source->signals & ~new_signals);
        source->signals = new_signals;
    }
}

static void usb_audio_source_read_complete(iotxn_t* txn, void* cookie) {
    usb_audio_source_t* source = (usb_audio_source_t*)cookie;

    if (txn->status == ERR_REMOTE_CLOSED) {
        txn->ops->release(txn);
        return;
    }

    mtx_lock(&source->mutex);
    if (source->open) {
        list_add_tail(&source->free_read_reqs, &txn->node);
    } else if (txn->status == NO_ERROR && txn->actual > 0) {
        list_add_tail(&source->completed_reads, &txn->node);
        source->completed_read_count++;

        // no client reading? requeue oldest completed read so we can keep reading new data
        if (list_is_empty(&source->free_read_reqs) &&
                          source->completed_read_count == READ_REQ_COUNT) {
            txn = list_remove_head_type(&source->completed_reads, iotxn_t, node);
            source->completed_read_count--;
            iotxn_queue(source->usb_device, txn);
        }
    } else {
        iotxn_queue(source->usb_device, txn);
    }
    update_signals(source);
    mtx_unlock(&source->mutex);
}

static void usb_audio_source_unbind(mx_device_t* device) {
    usb_audio_source_t* source = get_usb_audio_source(device);
    source->dead = true;
    update_signals(source);
    device_remove(&source->device);
}

static mx_status_t usb_audio_source_release(mx_device_t* device) {
    usb_audio_source_t* source = get_usb_audio_source(device);

    iotxn_t* txn;
    while ((txn = list_remove_head_type(&source->free_read_reqs, iotxn_t, node)) != NULL) {
        txn->ops->release(txn);
    }
    while ((txn = list_remove_head_type(&source->completed_reads, iotxn_t, node)) != NULL) {
        txn->ops->release(txn);
    }
    free(source->sample_rates);
    free(source);
    return NO_ERROR;
}

static mx_status_t usb_audio_source_start_locked(usb_audio_source_t* source) {
    if (source->dead) {
        return ERR_REMOTE_CLOSED;
    }
    if (source->started) {
        return NO_ERROR;
    }
    // switch to alternate interface if necessary
    if (!source->started && source->alternate_setting != 0) {
        usb_set_interface(source->usb_device, source->interface_number, source->alternate_setting);
    }

    // queue up reads, including stale completed reads
    iotxn_t* txn;
    while ((txn = list_remove_head_type(&source->completed_reads, iotxn_t, node)) != NULL) {
        iotxn_queue(source->usb_device, txn);
    }
    while ((txn = list_remove_head_type(&source->free_read_reqs, iotxn_t, node)) != NULL) {
        iotxn_queue(source->usb_device, txn);
    }

    source->started = true;
    return NO_ERROR;
}

static mx_status_t usb_audio_source_stop_locked(usb_audio_source_t* source) {
    if (source->dead) {
        return ERR_REMOTE_CLOSED;
    }
    // switch back to primary interface
    if (source->started && source->alternate_setting != 0) {
        usb_set_interface(source->usb_device, source->interface_number, 0);
    }
    source->started = false;
    return NO_ERROR;
}

static mx_status_t usb_audio_source_open(mx_device_t* dev, mx_device_t** dev_out, uint32_t flags) {
    usb_audio_source_t* source = get_usb_audio_source(dev);
    mx_status_t result;

    mtx_lock(&source->mutex);
    if (source->open) {
        result = ERR_ALREADY_BOUND;
    } else {
        source->open = true;
        result = NO_ERROR;
    }
    mtx_unlock(&source->mutex);

    return result;
}

static mx_status_t usb_audio_source_close(mx_device_t* dev) {
    usb_audio_source_t* source = get_usb_audio_source(dev);

    mtx_lock(&source->mutex);
    source->open = false;
    mtx_unlock(&source->mutex);

    return NO_ERROR;
}

static ssize_t usb_audio_source_read(mx_device_t* dev, void* data, size_t length, mx_off_t offset) {
    usb_audio_source_t* source = get_usb_audio_source(dev);

    if (source->dead) {
        return ERR_REMOTE_CLOSED;
    }

    mx_status_t status = 0;

    mtx_lock(&source->mutex);

    iotxn_t* txn = list_peek_head_type(&source->completed_reads, iotxn_t, node);
    if (!txn) {
        goto out;
    }
    // FIXME - for now we assume client reads with a buffer large enough for packet received
    size_t needed_bytes = (source->channels == 2) ? txn->actual : (txn->actual << 1);
    if (needed_bytes > length) {
        status = ERR_BUFFER_TOO_SMALL;
        goto out;
    }

    list_remove_head(&source->completed_reads);
    source->completed_read_count--;

    txn->ops->copyfrom(txn, data, txn->actual, 0);
    if (source->channels == 1) {
        // expand mono to stereo working backwards through the buffer
        uint16_t* start = (uint16_t *)data;
        uint16_t* src = (uint16_t *)(data + txn->actual - sizeof(uint16_t));
        uint16_t* dest = (uint16_t *)(data + 2 * txn->actual - sizeof(uint16_t));
        while (src >= start) {
            uint16_t sample = *src--;
            *dest-- = sample;
            *dest-- = sample;
        }
        status = 2 * txn->actual;
    } else {
        status = txn->actual;
    }

    // requeue the transaction
    if (source->dead) {
        txn->ops->release(txn);
    } else {
        iotxn_queue(source->usb_device, txn);
    }

out:
    update_signals(source);
    mtx_unlock(&source->mutex);
    return status;
}

static ssize_t usb_audio_source_ioctl(mx_device_t* dev, uint32_t op, const void* in_buf,
                                    size_t in_len, void* out_buf, size_t out_len) {
    usb_audio_source_t* source = get_usb_audio_source(dev);

    switch (op) {
    case IOCTL_AUDIO_GET_DEVICE_TYPE: {
        int* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_BUFFER_TOO_SMALL;
        *reply = AUDIO_TYPE_SOURCE;
        return sizeof(*reply);
    }
    case IOCTL_AUDIO_GET_SAMPLE_RATE_COUNT: {
        int* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_BUFFER_TOO_SMALL;
        *reply = source->sample_rate_count;
        return sizeof(*reply);
    }
    case IOCTL_AUDIO_GET_SAMPLE_RATES: {
        size_t reply_size = source->sample_rate_count * sizeof(uint32_t);
        if (out_len < reply_size) return ERR_BUFFER_TOO_SMALL;
        memcpy(out_buf, source->sample_rates, reply_size);
        return reply_size;
    }
    case IOCTL_AUDIO_GET_SAMPLE_RATE: {
        uint32_t* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_BUFFER_TOO_SMALL;
        *reply = source->sample_rate;
        return sizeof(*reply);
    }
    case IOCTL_AUDIO_SET_SAMPLE_RATE: {
        if (in_len < sizeof(uint32_t))  return ERR_BUFFER_TOO_SMALL;
        uint32_t sample_rate = *((uint32_t *)in_buf);
        if (sample_rate == source->sample_rate) return NO_ERROR;
        // validate sample rate
        int i;
        for (i = 0; i < source->sample_rate_count; i++) {
            if (sample_rate == source->sample_rates[i]) {
                break;
            }
        }
        if (i == source->sample_rate_count) {
            return ERR_INVALID_ARGS;
        }
        mx_status_t status = usb_audio_set_sample_rate(source->usb_device, source->ep_addr,
                                                       sample_rate);
        if (status == NO_ERROR) {
            source->sample_rate = sample_rate;
        }
        return status;
    }
    case IOCTL_AUDIO_START: {
        mtx_lock(&source->mutex);
        mx_status_t result = usb_audio_source_start_locked(source);
        mtx_unlock(&source->mutex);
        return result;
    }
    case IOCTL_AUDIO_STOP: {
        mtx_lock(&source->mutex);
        mx_status_t result = usb_audio_source_stop_locked(source);
        mtx_unlock(&source->mutex);
        return result;
    }
    }

    return ERR_NOT_SUPPORTED;
}

static mx_protocol_device_t usb_audio_source_device_proto = {
    .unbind = usb_audio_source_unbind,
    .release = usb_audio_source_release,
    .open = usb_audio_source_open,
    .close = usb_audio_source_close,
    .read = usb_audio_source_read,
    .ioctl = usb_audio_source_ioctl,
};

mx_status_t usb_audio_source_create(mx_driver_t* driver, mx_device_t* device, int index,
                                    usb_interface_descriptor_t* intf,
                                    usb_endpoint_descriptor_t* ep,
                                    usb_audio_ac_format_type_i_desc* format_desc) {
    if (!format_desc) {
        printf("no audio format descriptor found in usb_audio_source_create\n");
        return ERR_INVALID_ARGS;
    }
    if ((format_desc->bNrChannels != 1 && format_desc->bNrChannels != 2) ||
            format_desc->bSubFrameSize != 2 ||format_desc->bBitResolution != 16) {
        printf("unsupported audio format in usb_audio_source_create\n");
        return ERR_INVALID_ARGS;
    }

    usb_audio_source_t* source = calloc(1, sizeof(usb_audio_source_t));
    if (!source) {
        printf("Not enough memory for usb_audio_source_t\n");
        return ERR_NO_MEMORY;
    }
    source->sample_rates = usb_audio_parse_sample_rates(format_desc, &source->sample_rate_count);
    if (!source->sample_rates) {
        free(source);
        return ERR_NO_MEMORY;
    }
    source->channels = format_desc->bNrChannels;

    list_initialize(&source->free_read_reqs);
    list_initialize(&source->completed_reads);

    source->usb_device = device;
    source->ep_addr = ep->bEndpointAddress;
    source->interface_number = intf->bInterfaceNumber;
    source->alternate_setting = intf->bAlternateSetting;

    int packet_size = usb_ep_max_packet(ep);

    for (int i = 0; i < READ_REQ_COUNT; i++) {
        iotxn_t* txn = usb_alloc_iotxn(source->ep_addr, packet_size, 0);
        if (!txn)
            return ERR_NO_MEMORY;
        txn->length = packet_size;
        txn->complete_cb = usb_audio_source_read_complete;
        txn->cookie = source;
        list_add_head(&source->free_read_reqs, &txn->node);
    }

    source->sample_rate = source->sample_rates[0];
    // this may stall if only one sample rate is supported, so ignore error
    usb_audio_set_sample_rate(source->usb_device, source->ep_addr, source->sample_rate);

    char name[MX_DEVICE_NAME_MAX];
    snprintf(name, sizeof(name), "usb-audio-source-%d\n", index);
    device_init(&source->device, driver, name, &usb_audio_source_device_proto);

    source->device.protocol_id = MX_PROTOCOL_AUDIO;
    source->device.protocol_ops = NULL;
    mx_status_t status = device_add(&source->device, source->usb_device);
    if (status != NO_ERROR) {
        printf("device_add failed in usb_audio_bind\n");
        usb_audio_source_release(&source->device);
    }

    return status;
}
