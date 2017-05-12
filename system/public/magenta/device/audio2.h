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
    AUDIO2_STREAM_CMD_SET_FORMAT     = 0x1000,
    AUDIO2_STREAM_CMD_GET_GAIN       = 0x1001,
    AUDIO2_STREAM_CMD_SET_GAIN       = 0x1002,
    AUDIO2_STREAM_CMD_PLUG_DETECT    = 0x1003,

    // Async notifications sent on the stream channel.
    AUDIO2_STREAM_PLUG_DETECT_NOTIFY = 0x2000,

    // Commands sent on the ring buffer channel
    AUDIO2_RB_CMD_GET_FIFO_DEPTH     = 0x3000,
    AUDIO2_RB_CMD_GET_BUFFER         = 0x3001,
    AUDIO2_RB_CMD_START              = 0x3002,
    AUDIO2_RB_CMD_STOP               = 0x3003,

    // Async notifications sent on the ring buffer channel.
    AUDIO2_RB_POSITION_NOTIFY        = 0x4000,

    // Flags used to modify commands.
    AUDIO2_FLAG_NO_ACK               = 0x80000000,
} audio2_cmd_t;

static_assert(sizeof(audio2_cmd_t) == sizeof(uint32_t),
              "audio2_cmd_t must be 32 bits!\n");

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

static_assert(sizeof(audio2_sample_format_t) == sizeof(uint32_t),
              "audio2_sample_format_t must be 32 bits!\n");

// audio2_set_gain_flags_t
//
// Flags used by the AUDIO2_STREAM_CMD_SET_GAIN message.
//
typedef enum audio2_set_gain_flags {
    AUDIO2_SGF_MUTE_VALID = 0x1,        // Whether or not the mute flag is valid.
    AUDIO2_SGF_GAIN_VALID = 0x2,        // Whether or not the gain float is valid.
    AUDIO2_SGF_MUTE       = 0x80000000, // Whether or not to mute the stream.
} audio2_set_gain_flags_t;

static_assert(sizeof(audio2_set_gain_flags_t) == sizeof(uint32_t),
              "audio2_set_gain_flags_t must be 32 bits!\n");

// audio2_pd_flags_t
//
// Flags used by AUDIO2_STREAM_CMD_PLUG_DETECT commands to enable or disable
// asynchronous plug detect notifications.
//
typedef enum audio2_pd_flags {
    AUDIO2_PDF_NONE                  = 0,
    AUDIO2_PDF_ENABLE_NOTIFICATIONS  = 0x40000000,
    AUDIO2_PDF_DISABLE_NOTIFICATIONS = 0x80000000,
} audio2_pd_flags_t;

static_assert(sizeof(audio2_pd_flags_t) == sizeof(uint32_t),
              "audio2_pd_flags_t must be 32 bits!\n");

// audio2_pd_notify_flags_t
//
// Flags used by responses to the AUDIO2_STREAM_CMD_PLUG_DETECT
// message, and by AUDIO2_STREAM_PLUG_DETECT_NOTIFY messages.
//
typedef enum audio2_pd_notify_flags {
    AUDIO2_PDNF_HARDWIRED  = 0x1,        // Stream is hardwired (will always be plugged in)
    AUDIO2_PDNF_CAN_NOTIFY = 0x2,        // Stream is able to notify of plug state changes.
    AUDIO2_PDNF_PLUGGED    = 0x80000000, // Stream is currently plugged in.
} audio2_pd_notify_flags_t;

static_assert(sizeof(audio2_pd_notify_flags_t) == sizeof(uint32_t),
              "audio2_pd_resp_flags_t must be 32 bits!\n");

// AUDIO2_STREAM_CMD_SET_FORMAT
//
// May not be used with the NO_ACK flag.
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

// AUDIO2_STREAM_CMD_GET_GAIN
//
// Request that a gain notification be sent with the current details of the
// streams current gain settings as well as gain setting capabilities.
//
// May not be used with the NO_ACK flag.
typedef struct audio2_stream_cmd_get_gain_req {
    audio2_cmd_hdr_t hdr;
} audio2_stream_cmd_get_gain_req_t;

typedef struct audio2_stream_cmd_get_gain_resp {
    // TODO(johngro) : Is there value in exposing the gain step to the level
    // above the lowest level stream interface, or should we have all drivers
    // behave as if they have continuous control at all times?
    audio2_cmd_hdr_t hdr;

    bool             cur_mute;  // True if the amplifier is currently muted.
    float            cur_gain;  // The current setting gain of the amplifier in dB

    bool             can_mute;  // True if the amplifier is capable of muting
    float            min_gain;  // The minimum valid gain setting, in dB
    float            max_gain;  // The maximum valid gain setting, in dB
    float            gain_step; // The smallest valid gain increment, counted from the minimum gain.
} audio2_stream_cmd_get_gain_resp_t;

// AUDIO2_STREAM_CMD_SET_GAIN
//
// Request that a stream change its gain settings to most closely match those
// requested.  Gain values for Valid requests will be rounded to the nearest
// gain step.  For example, if a stream can control its gain on the range from
// -60.0 to 0.0 dB, a request to set the gain to -33.3 dB will result in a gain
// of -33.5 being applied.
//
// Gain change requests outside of the capabilities of the stream's
// amplifier will be rejected with a result of ERR_INVALID_ARGS.  Using the
// previous example, requests for gains of -65.0 or +3dB would be rejected.
// Similarly,  If an amplifier is capable of gain control but cannot mute, a
// request to mute will be rejected.
//
// TODO(johngro) : Is this the correct behavior?  Should we just apply sensible
// limits instead?  IOW - If the user requests a gain of -1000 dB, should we
// just set the gain to -60dB?  Likewise, if they request mute but the amplifier
// has no hard mute feature, should we just set the gain to the minimum
// permitted gain?
//
// May be used with the NO_ACK flag.
typedef struct audio2_stream_cmd_set_gain_req {
    audio2_cmd_hdr_t        hdr;
    audio2_set_gain_flags_t flags;
    float                   gain;
} audio2_stream_cmd_set_gain_req_t;

typedef struct audio2_stream_cmd_set_gain_resp {
    audio2_cmd_hdr_t hdr;
    mx_status_t      result;
    // The current gain settings observed immediately after processing the set
    // gain request.
    bool             cur_mute;
    float            cur_gain;
} audio2_stream_cmd_set_gain_resp_t;

// AUDIO2_STREAM_CMD_PLUG_DETECT
//
// Trigger a plug detect operation and/or enable/disable asynchronous plug
// detect notifications.
//
typedef struct audio2_stream_cmd_plug_detect_req {
    audio2_cmd_hdr_t  hdr;
    audio2_pd_flags_t flags;  // Options used to enable or disable notifications
} audio2_stream_cmd_plug_detect_req_t;

typedef struct audio2_stream_cmd_plug_detect_resp {
    audio2_cmd_hdr_t         hdr;
    audio2_pd_notify_flags_t flags;           // The current plug state and capabilities
    mx_time_t                plug_state_time; // The time of the plug state last change.
} audio2_stream_cmd_plug_detect_resp_t;

// AUDIO2_STREAM_PLUG_DETECT_NOTIFY
//
// Message asynchronously in response to a plug state change to clients who have
// registered for plug state notifications.
//
// Note: Solicited and unsolicited plug detect messages currently use the same
// structure and contain the same information.  The difference between the two
// is that Solicited messages, use AUDIO2_STREAM_CMD_PLUG_DETECT as the value of
// the `cmd` field of their header and the transaction ID of the request sent by
// the client.  Unsolicited messages use AUDIO2_STREAM_PLUG_DETECT_NOTIFY as the
// value value of the `cmd` field of their header, and
// AUDIO2_INVALID_TRANSACTION_ID for their transaction ID.
typedef audio2_stream_cmd_plug_detect_resp_t audio2_stream_plug_detect_notify_t;

// AUDIO2_RB_CMD_GET_FIFO_DEPTH
//
// TODO(johngro) : Is calling this "FIFO" depth appropriate?  Should it be some
// direction neutral form of something like "max-read-ahead-amount" or something
// instead?
//
// May be not used with the NO_ACK flag.
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
