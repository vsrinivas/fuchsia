// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/common/usb.h>
#include <magenta/device/audio.h>
#include <magenta/device/usb.h>
#include <magenta/hw/usb-audio.h>
#include <sync/completion.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#include "usb-audio.h"

#define WRITE_REQ_COUNT 20

// Assume audio is paused and reset our timer logic
// if no writes occur for 100ms
#define WRITE_TIMEOUT_MS 100

typedef struct {
    mx_device_t* mxdev;
    mx_device_t* usb_mxdev;
    uint8_t ep_addr;
    uint8_t interface_number;
    uint8_t alternate_setting;

    // pool of free USB requests
    list_node_t free_write_reqs;
    // mutex for synchronizing access to free_write_reqs, open and started
    mtx_t mutex;
    // completion signals free_write_reqs not empty
    completion_t free_write_completion;
    // mutex used to synchronize ioctl_audio_start() and ioctl_audio_stop()
    mtx_t start_stop_mutex;

    bool open;
    bool started;
    bool dead;

    // supported sample rates
    uint32_t* sample_rates;
    int sample_rate_count;
    // current sample rate
    uint32_t sample_rate;
    int num_channels;
    int audio_frame_size; // size of an audio frame

    // partially filled iotxn with data left over from last write() call
    // cur_txn->length marks size of left over data
    iotxn_t* cur_txn;

    // USB frame we started playing at
    uint64_t start_usb_frame;
    // last USB frame we scheduled a packet for
    uint64_t last_usb_frame;
    // audio frames written since start_usb_frame
    uint64_t audio_frame_count;

    // the last signals we reported
    mx_signals_t signals;

} usb_audio_sink_t;

static void update_signals(usb_audio_sink_t* sink) {
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

static void usb_audio_sink_write_complete(iotxn_t* txn, void* cookie) {
    if (txn->status == ERR_PEER_CLOSED) {
        iotxn_release(txn);
        return;
    }

    usb_audio_sink_t* sink = (usb_audio_sink_t*)cookie;
    // FIXME what to do with error here?
    mtx_lock(&sink->mutex);
    list_add_tail(&sink->free_write_reqs, &txn->node);
    completion_signal(&sink->free_write_completion);
    update_signals(sink);
    mtx_unlock(&sink->mutex);
}

static void usb_audio_sink_unbind(void* ctx) {
    usb_audio_sink_t* sink = ctx;
    sink->dead = true;
    update_signals(sink);
    completion_signal(&sink->free_write_completion);
    device_remove(sink->mxdev);
}

static void usb_audio_sink_free(usb_audio_sink_t* sink) {
    iotxn_t* txn;
    while ((txn = list_remove_head_type(&sink->free_write_reqs, iotxn_t, node)) != NULL) {
        iotxn_release(txn);
    }
    free(sink->sample_rates);
    free(sink);
}

static void usb_audio_sink_release(void* ctx) {
    usb_audio_sink_t* sink = ctx;
    usb_audio_sink_free(sink);
}

static uint64_t get_usb_current_frame(usb_audio_sink_t* sink) {
    uint64_t result;
    size_t actual = 0;
    mx_status_t status = device_op_ioctl(sink->usb_mxdev, IOCTL_USB_GET_CURRENT_FRAME,
                                 NULL, 0, &result, sizeof(result), &actual);
    if (status != NO_ERROR || actual != sizeof(result)) {
        printf("get_usb_current_frame failed %u\n",status);
        return sink->last_usb_frame;
    }
    return result;
}

static mx_status_t usb_audio_sink_start(usb_audio_sink_t* sink) {
    mx_status_t status = NO_ERROR;

    mtx_lock(&sink->start_stop_mutex);
    if (sink->dead) {
        status = ERR_PEER_CLOSED;
        goto out;
    }
    if (sink->started) {
        goto out;
    }

    // switch to alternate interface if necessary
    if (sink->alternate_setting != 0) {
        usb_set_interface(sink->usb_mxdev, sink->interface_number, sink->alternate_setting);
    }
    sink->start_usb_frame = 0;
    sink->cur_txn = NULL;

out:
    mtx_unlock(&sink->start_stop_mutex);
    return status;
}

static mx_status_t usb_audio_sink_stop(usb_audio_sink_t* sink) {
    mx_status_t status = NO_ERROR;

    mtx_lock(&sink->start_stop_mutex);
    if (sink->dead) {
        status = ERR_PEER_CLOSED;
        goto out;
    }
    if (!sink->started) {
        goto out;
    }

    // switch back to primary interface
    if (sink->alternate_setting != 0) {
        usb_set_interface(sink->usb_mxdev, sink->interface_number, 0);
    }

out:
    mtx_unlock(&sink->start_stop_mutex);
    return status;
}

static mx_status_t usb_audio_sink_open(void* ctx, mx_device_t** dev_out, uint32_t flags) {
    usb_audio_sink_t* sink = ctx;
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

static mx_status_t usb_audio_sink_close(void* ctx, uint32_t flags) {
    usb_audio_sink_t* sink = ctx;

    mtx_lock(&sink->mutex);
    sink->open = false;
    mtx_unlock(&sink->mutex);
    usb_audio_sink_stop(sink);

    return NO_ERROR;
}

static mx_status_t usb_audio_sink_write(void* ctx, const void* data, size_t length,
                                        mx_off_t offset, size_t* actual) {
    usb_audio_sink_t* sink = ctx;

    if (sink->dead) {
        return ERR_PEER_CLOSED;
    }

    mx_status_t status = NO_ERROR;
    size_t out_actual = length;

    const void* src = data;

    uint64_t current_frame = get_usb_current_frame(sink);
    if (sink->start_usb_frame == 0 || current_frame > sink->last_usb_frame + WRITE_TIMEOUT_MS) {
        // This is either the first time we are called or we have paused playing for awhile
        // so reset our counters
        sink->start_usb_frame = current_frame;
        sink->last_usb_frame = current_frame;
        sink->audio_frame_count = 0;
    }

    while (length > 0) {
        // get a free iotxn (might be a partially filled one in sink->cur_txn
        iotxn_t* txn;
        mx_off_t txn_offset;
        if (sink->cur_txn) {
            txn = sink->cur_txn;
            sink->cur_txn = NULL;
            txn_offset = txn->length;
        } else {
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
            txn = containerof(node, iotxn_t, node);
            txn_offset = 0;
        }

        uint64_t current_usb_frame = sink->last_usb_frame + 1;
        // total number of frames we should have sent by current_usb_frame
        uint64_t total_audio_frames = ((current_usb_frame - sink->start_usb_frame) *
                                       sink->sample_rate) / 1000;
        uint64_t current_audio_frames = total_audio_frames - sink->audio_frame_count;
        uint64_t packet_bytes = current_audio_frames * sink->audio_frame_size;
        uint64_t copy = packet_bytes - txn_offset;
        if (copy <= length) {
            iotxn_copyto(txn, src, copy, txn_offset);
            txn->length = txn_offset + copy;
            src += copy;
            length -= copy;

            usb_iotxn_set_frame(txn, current_usb_frame);
            iotxn_queue(sink->usb_mxdev, txn);

            sink->last_usb_frame = current_usb_frame;
            sink->audio_frame_count += current_audio_frames;
        } else {
            // not enough data remaining - save for next time
            sink->cur_txn = txn;
            iotxn_copyto(txn, src, length, 0);
            txn->length = length;
            length = 0;
        }
    }

out:
    update_signals(sink);
    if (status == NO_ERROR) {
        *actual = out_actual;
    }
    return status;
}

static mx_status_t usb_audio_sink_ioctl(void* ctx, uint32_t op, const void* in_buf,
                                        size_t in_len, void* out_buf, size_t out_len,
                                        size_t* out_actual) {
    usb_audio_sink_t* sink = ctx;

    switch (op) {
    case IOCTL_AUDIO_GET_DEVICE_TYPE: {
        int* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_BUFFER_TOO_SMALL;
        *reply = AUDIO_TYPE_SINK;
        *out_actual = sizeof(*reply);
        return NO_ERROR;
    }
    case IOCTL_AUDIO_GET_SAMPLE_RATE_COUNT: {
        int* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_BUFFER_TOO_SMALL;
        *reply = sink->sample_rate_count;
        *out_actual = sizeof(*reply);
        return NO_ERROR;
    }
    case IOCTL_AUDIO_GET_SAMPLE_RATES: {
        size_t reply_size = sink->sample_rate_count * sizeof(uint32_t);
        if (out_len < reply_size) return ERR_BUFFER_TOO_SMALL;
        memcpy(out_buf, sink->sample_rates, reply_size);
        *out_actual = reply_size;
        return NO_ERROR;
    }
    case IOCTL_AUDIO_GET_SAMPLE_RATE: {
        uint32_t* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_BUFFER_TOO_SMALL;
        *reply = sink->sample_rate;
        *out_actual = sizeof(*reply);
        return NO_ERROR;
    }
    case IOCTL_AUDIO_SET_SAMPLE_RATE: {
        if (in_len < sizeof(uint32_t))  return ERR_BUFFER_TOO_SMALL;
        uint32_t sample_rate = *((uint32_t *)in_buf);
        if (sample_rate == sink->sample_rate) return NO_ERROR;
        // validate sample rate
        int i;
        for (i = 0; i < sink->sample_rate_count; i++) {
            if (sample_rate == sink->sample_rates[i]) {
                break;
            }
        }
        if (i == sink->sample_rate_count) {
            return ERR_INVALID_ARGS;
        }
        mx_status_t status = usb_audio_set_sample_rate(sink->usb_mxdev, sink->ep_addr, sample_rate);
        if (status == NO_ERROR) {
            sink->sample_rate = sample_rate;
        }
        return status;
    }
    case IOCTL_AUDIO_START:
        return usb_audio_sink_start(sink);
    case IOCTL_AUDIO_STOP:
        return usb_audio_sink_stop(sink);
    }

    return ERR_NOT_SUPPORTED;
}

static mx_protocol_device_t usb_audio_sink_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = usb_audio_sink_unbind,
    .release = usb_audio_sink_release,
    .open = usb_audio_sink_open,
    .close = usb_audio_sink_close,
    .write = usb_audio_sink_write,
    .ioctl = usb_audio_sink_ioctl,
};

mx_status_t usb_audio_sink_create(mx_device_t* device, int index,
                                  usb_interface_descriptor_t* intf,
                                  usb_endpoint_descriptor_t* ep,
                                  usb_audio_ac_format_type_i_desc* format_desc) {
    if (!format_desc) {
        printf("no audio format descriptor found in usb_audio_sink_create\n");
        return ERR_INVALID_ARGS;
    }
    if (format_desc->bNrChannels != 2 || format_desc->bSubFrameSize != 2 ||
        format_desc->bBitResolution != 16) {
        printf("unsupported audio format in usb_audio_sink_create\n");
        return ERR_INVALID_ARGS;
    }

    usb_audio_sink_t* sink = calloc(1, sizeof(usb_audio_sink_t));
    if (!sink) {
        printf("Not enough memory for usb_audio_sink_t\n");
        return ERR_NO_MEMORY;
    }
    sink->sample_rates = usb_audio_parse_sample_rates(format_desc, &sink->sample_rate_count);
    if (!sink->sample_rates) {
        free(sink);
        return ERR_NO_MEMORY;
    }

    list_initialize(&sink->free_write_reqs);

    sink->usb_mxdev = device;
    sink->ep_addr = ep->bEndpointAddress;
    sink->interface_number = intf->bInterfaceNumber;
    sink->alternate_setting = intf->bAlternateSetting;
    int packet_size = usb_ep_max_packet(ep);

    for (int i = 0; i < WRITE_REQ_COUNT; i++) {
        iotxn_t* txn = usb_alloc_iotxn(sink->ep_addr, packet_size);
        if (!txn) {
            usb_audio_sink_free(sink);
            return ERR_NO_MEMORY;
        }
        txn->length = packet_size;
        txn->complete_cb = usb_audio_sink_write_complete;
        txn->cookie = sink;
        list_add_head(&sink->free_write_reqs, &txn->node);
    }
    completion_signal(&sink->free_write_completion);

    // only support 2 channel with 16 bit samples for now
    sink->num_channels = 2;
    sink->audio_frame_size = sink->num_channels * sizeof(uint16_t);
    sink->sample_rate = sink->sample_rates[0];

    if (sink->sample_rate_count > 1) {
        // this may stall if only one sample rate is supported, so only call this if
        // multiple sample rates are supported
        mx_status_t status = usb_audio_set_sample_rate(sink->usb_mxdev, sink->ep_addr,
                                                       sink->sample_rate);
        if (status != NO_ERROR) {
            printf("usb_audio_set_sample_rate failed in usb_audio_sink_create\n");
            usb_audio_sink_free(sink);
            return status;
        }
    }

    char name[MX_DEVICE_NAME_MAX];
    snprintf(name, sizeof(name), "usb-audio-sink-%d\n", index);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = sink,
        .ops = &usb_audio_sink_device_proto,
        .proto_id = MX_PROTOCOL_AUDIO,
    };

    mx_status_t status = device_add(device, &args, &sink->mxdev);
    if (status != NO_ERROR) {
        printf("device_add failed in usb_audio_sink_create\n");
        usb_audio_sink_free(sink);
    }

    return status;
}
