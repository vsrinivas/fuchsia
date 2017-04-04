// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>
#include <magenta/types.h>

#define AUDIO2_IOCTL_GET_CHANNEL IOCTL(IOCTL_KIND_GET_HANDLE, 0xFE, 0x00)
IOCTL_WRAPPER_OUT(ioctl_audio2_get_channel, AUDIO2_IOCTL_GET_CHANNEL, mx_handle_t);

// When communicating with an Audio2 driver using mx_channel_call, do not use
// the AUDIO2_INVALID_TRANSACTION_ID as your message's transaction ID.  It is
// reserved for async notifications sent from the driver to the application.
#define AUDIO2_INVALID_TRANSACTION_ID ((mx_txid_t)0)

__BEGIN_CDECLS

typedef enum audio2_cmd {
    // Commands sent on the stream channel
    AUDIO2_STREAM_CMD_SET_FORMAT = 0x1000,

    // Commands sent on the ring buffer channel
    AUDIO2_RB_CMD_GET_FIFO_DEPTH = 0x2000,
    AUDIO2_RB_CMD_GET_BUFFER     = 0x2001,
    AUDIO2_RB_CMD_START          = 0x2002,
    AUDIO2_RB_CMD_STOP           = 0x2003,

    // Async notifications sent on the ring buffer channel.
    AUDIO2_RB_POSITION_NOTIFY    = 0x3000,
} audio2_cmd_t;

typedef struct audio2_cmd_hdr {
    mx_txid_t    transaction_id;
    audio2_cmd_t cmd;
} audio2_cmd_hdr_t;

// audio2_sample_format_t
//
// Bitfield which describes audio sample format as they reside in memory.
//
typedef enum audio2_sample_format {
    AUDIO2_SAMPLE_FORMAT_BITSTREAM    = (1u << 0),
    AUDIO2_SAMPLE_FORMAT_8BIT         = (1u << 1),
    AUDIO2_SAMPLE_FORMAT_16BIT        = (1u << 2),
    AUDIO2_SAMPLE_FORMAT_20BIT_PACKED = (1u << 4),
    AUDIO2_SAMPLE_FORMAT_24BIT_PACKED = (1u << 5),
    AUDIO2_SAMPLE_FORMAT_20BIT_IN32   = (1u << 6),
    AUDIO2_SAMPLE_FORMAT_24BIT_IN32   = (1u << 7),
    AUDIO2_SAMPLE_FORMAT_32BIT        = (1u << 8),
    AUDIO2_SAMPLE_FORMAT_32BIT_FLOAT  = (1u << 9),

    AUDIO2_SAMPLE_FORMAT_FLAG_UNSIGNED      = (1u << 30),
    AUDIO2_SAMPLE_FORMAT_FLAG_INVERT_ENDIAN = (1u << 31),
    AUDIO2_SAMPLE_FORMAT_FLAG_MASK = AUDIO2_SAMPLE_FORMAT_FLAG_UNSIGNED |
                                     AUDIO2_SAMPLE_FORMAT_FLAG_INVERT_ENDIAN,
} audio2_sample_format_t;

// AUDIO2_STREAM_CMD_SET_FORMAT
typedef struct audio2_stream_cmd_set_format_req {
    audio2_cmd_hdr_t       hdr;
    uint32_t               frames_per_second;
    audio2_sample_format_t sample_format;
    uint16_t               channels;
} audio2_stream_cmd_set_format_req_t;

typedef struct audio2_stream_cmd_set_format_resp {
    audio2_cmd_hdr_t hdr;
    mx_status_t      result;

    // Note: Upon success, a channel used to control the audio buffer will also
    // be returned.
} audio2_stream_cmd_set_format_resp_t;

// AUDIO2_RB_CMD_GET_FIFO_DEPTH
//
// TODO(johngro) : Is calling this "FIFO" depth appropriate?  Should it be some
// direction neutral form of something like "max-read-ahead-amount" or something
// instead?
typedef struct audio2_rb_cmd_get_fifo_depth_req {
    audio2_cmd_hdr_t hdr;
} audio2_rb_cmd_get_fifo_depth_req_t;

typedef struct audio2_rb_cmd_get_fifo_depth_resp {
    audio2_cmd_hdr_t hdr;
    mx_status_t      result;

    // A representation (in bytes) of how far ahead audio hardware may read
    // into the stream (in the case of output) or may hold onto audio before
    // writing it to memory (in the case of input).
    uint32_t fifo_depth;
} audio2_rb_cmd_get_fifo_depth_resp_t;

// AUDIO2_RB_CMD_GET_BUFFER
typedef struct audio2_rb_cmd_get_buffer_req {
    audio2_cmd_hdr_t hdr;

    uint32_t min_ring_buffer_frames;
    uint32_t notifications_per_ring;
} audio2_rb_cmd_get_buffer_req_t;

typedef struct audio2_rb_cmd_get_buffer_resp {
    audio2_cmd_hdr_t hdr;
    mx_status_t      result;

    // NOTE: If result == NO_ERROR, a VMO handle representing the ring buffer to
    // be used will be returned as well.  Clients may map this buffer with
    // read-write permissions in the case of an output stream, or read-only
    // permissions in the case of an input stream.  The size of the VMO
    // indicates where the wrap point of the ring (in bytes) is located in the
    // VMO.  This size *must* always be an integral number of audio frames.
    //
    // TODO(johngro) : Should we provide some indication of whether or not this
    // memory is being used directly for HW DMA and may need explicit cache
    // flushing/invalidation?
} audio2_rb_cmd_get_buffer_resp_t;

// AUDIO2_RB_CMD_START
typedef struct audio2_rb_cmd_start_req {
    audio2_cmd_hdr_t hdr;
} audio2_rb_cmd_start_req_t;

typedef struct audio2_rb_cmd_start_resp {
    audio2_cmd_hdr_t hdr;
    mx_status_t      result;

    // Nominal time at which the first frame of audio started to be clocked out
    // to the codec as measured by mx_ticks_get().
    //
    uint64_t start_ticks;
} audio2_rb_cmd_start_resp_t;

// AUDIO2_RB_CMD_STOP
typedef struct audio2_rb_cmd_stop_req {
    audio2_cmd_hdr_t hdr;
} audio2_rb_cmd_stop_req_t;

typedef struct audio2_rb_cmd_stop_resp {
    audio2_cmd_hdr_t hdr;
    mx_status_t      result;
} audio2_rb_cmd_stop_resp_t;

// AUDIO2_RB_POSITION_NOTIFY
typedef struct audio2_rb_position_notify {
    audio2_cmd_hdr_t hdr;

    // The current position (in bytes) of the driver/hardware's read (output) or
    // write (input) pointer in the ring buffer.
    uint32_t ring_buffer_pos;
} audio2_rb_position_notify_t;

__END_CDECLS
