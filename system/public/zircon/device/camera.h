// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>

#define CAMERA_IOCTL_GET_CHANNEL IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_CAMERA, 0)
IOCTL_WRAPPER_OUT(ioctl_camera_get_channel, CAMERA_IOCTL_GET_CHANNEL, zx_handle_t);

__BEGIN_CDECLS

typedef enum camera_cmd {
    // Commands sent on the stream channel.
    CAMERA_STREAM_CMD_GET_FORMATS      = 0x1000,
    CAMERA_STREAM_CMD_SET_FORMAT       = 0x1001,

    // Commands sent on the ring buffer channel
    CAMERA_RB_CMD_GET_DATA_BUFFER      = 0x2000,
    CAMERA_RB_CMD_GET_METADATA_BUFFER  = 0x2001,
    CAMERA_RB_CMD_START                = 0x2002,
    CAMERA_RB_CMD_STOP                 = 0x2003,
    CAMERA_RB_CMD_FRAME_LOCK           = 0x2004,
    CAMERA_RB_CMD_FRAME_RELEASE        = 0x2005,

    // Async notifications sent on the ring buffer channel.
    CAMERA_RB_METADATA_POSITION_NOTIFY = 0x3000,
} camera_cmd_t;

// Common header for all camera requests and responses.
typedef struct camera_cmd_hdr {
    camera_cmd_t cmd;
} camera_cmd_hdr_t;

// camera_capture_type_t
//
// Describes the type of data expected in the data buffer.
typedef enum camera_capture_type {
    STILL_IMAGE = 0x1,  // The source will provide on image.
    BURST,              // The source will provide a set of images.
    STREAM              // The source will be continuously providing frames
                        // until signalled to stop.
} camera_capture_type_t;

// camera_pixel_format_t
//
// TODO(garratt): Randomly came up with this. Revisit.
// Check out go/fuchsia-pixel-formats for more debate
typedef enum camera_pixel_format {
    INVALID,      // default value, not supported
    RGB32,        // 32bpp BGRA, 1 plane.

    NV12,
    YUY2,

    MJPEG
} camera_pixel_format_t;

// camera_video_format_range_t
//
// A structure used along with the CAMERA_STREAM_CMD_GET_FORMATS command
// in order to describe the formats supported by a video stream.
typedef struct camera_video_format {
    camera_capture_type_t capture_type;
    uint16_t width;
    uint16_t height;
    uint8_t bits_per_pixel;
    camera_pixel_format_t pixel_format;
    // The frame rate is frames_per_nsec_numerator / frames_per_nsec_denominator.
    uint32_t frames_per_nsec_numerator;
    uint32_t frames_per_nsec_denominator;

    // To deal with temporal encoding we will need
    // uint32 frames_per_packet;
} __PACKED camera_video_format_t;

// camera_metadata_t
//
// Describes the characteristics of the corresponding frame in the
// data ring buffer.
typedef struct camera_metadata {
    // Identifier for the frame.
    uint32_t frame_number;
    // The position (in bytes) of the frame in the data buffer.
    uint32_t data_rb_offset;
    // Number of bytes in the frame.
    uint32_t frame_size;

    camera_video_format_t format;

    uint32_t presentation_timestamp;
    uint32_t source_time_clock;
    uint32_t clock_frequency_hz;

    // Other fields such as zoom level may be added in future.
} camera_metadata_t;

// CAMERA_STREAM_CMD_GET_FORMATS
#define CAMERA_STREAM_CMD_GET_FORMATS_MAX_FORMATS_PER_RESPONSE (16u)

typedef struct camera_stream_cmd_get_formats_req {
    camera_cmd_hdr_t hdr;
} camera_stream_cmd_get_formats_req_t;

typedef struct camera_stream_cmd_get_formats_resp {
    camera_cmd_hdr_t hdr;

    // This may be greater than CAMERA_MAX_FORMATS_PER_RESPONSE,
    // in which case the client should wait on the channel
    // for additional camera_cmd_get_formats_resp messages.
    uint16_t total_format_count;
    // The total number of formats sent in all previous messages
    // of the request.
    uint16_t already_sent_count;
    camera_video_format_t formats[CAMERA_STREAM_CMD_GET_FORMATS_MAX_FORMATS_PER_RESPONSE];
} camera_stream_cmd_get_formats_resp_t;

// CAMERA_STREAM_CMD_SET_FORMAT
//
// Sent by the client to indicate desired stream characteristics.
typedef struct camera_stream_cmd_set_format_req {
    camera_cmd_hdr_t hdr;

    camera_video_format video_format;
} camera_stream_cmd_set_format_req_t;

typedef struct camera_stream_cmd_set_format_resp {
    camera_cmd_hdr_t hdr;
    zx_status_t result;

    // NOTE: Upon success, a channel used to control the video buffer will also
    // be returned.
} camera_stream_cmd_set_format_resp_t;

// CAMERA_RB_CMD_GET_DATA_BUFFER
typedef struct camera_rb_cmd_get_data_buffer_req {
    camera_cmd_hdr_t hdr;
} camera_rb_cmd_get_data_buffer_req_t;

typedef struct camera_rb_cmd_get_data_buffer_resp {
    camera_cmd_hdr_t hdr;
    zx_status_t     result;

    uint32_t max_frame_size;
    // NOTE: If result == ZX_OK, a VMO handle representing the data ring buffer to
    // be used will be returned as well.  Clients may map this buffer with
    // read-only permissions. The size of the VMO indicates there the wrap point
    // of the ring (in bytes) is located in the VMO.
    // This size *must* always be a integral multiple of the maximum frame size.
    // Frames will be aligned to maximum frame size.
} camera_rb_cmd_get_data_buffer_resp_t;

// CAMERA_RB_CMD_GET_METADATA_BUFFER
typedef struct camera_rb_cmd_get_metadata_buffer_req {
    camera_cmd_hdr_t hdr;
} camera_rb_cmd_get_metadata_buffer_req_t;

typedef struct camera_rb_cmd_get_metadata_buffer_resp {
    camera_cmd_hdr_t hdr;
    zx_status_t     result;

    // NOTE: If result == ZX_OK, a VMO handle representing the metadata ring buffer to
    // be used will be returned as well.  Clients may map this buffer with
    // read-only permissions. The size of the VMO indicates there the wrap point
    // of the ring (in bytes) is located in the VMO.
    // This size *must* always be an integral number of metadata entries.
} camera_rb_cmd_get_metadata_buffer_resp_t;

// CAMERA_RB_CMD_START
//
// Starts the streaming of frames.
typedef struct camera_rb_cmd_start_req {
    camera_cmd_hdr_t hdr;
} camera_rb_cmd_start_req_t;

typedef struct camera_rb_cmd_start_resp {
    camera_cmd_hdr_t hdr;
    zx_status_t     result;
} camera_rb_cmd_start_resp_t;

// CAMERA_RB_CMD_STOP
//
// Stops the streaming of frames.
typedef struct camera_rb_cmd_stop_req {
    camera_cmd_hdr_t hdr;
} camera_rb_cmd_stop_req_t;

typedef struct camera_rb_cmd_stop_resp {
    camera_cmd_hdr_t hdr;
    zx_status_t     result;
} camera_rb_cmd_stop_resp_t;

// CAMERA_RB_CMD_FRAME_LOCK
//
// Locks the specified frame. If successful, the driver will not overwrite the
// specified frame until it receives a CAMERA_RB_CMD_FRAME_RELEASE command
// for the frame.
// Clients should lock frames for as short a duration as possible.
typedef struct camera_rb_cmd_frame_lock_req {
    camera_cmd_hdr_t hdr;
    uint32_t frame_number;
} camera_rb_cmd_frame_lock_req_t;

typedef struct camera_rb_cmd_frame_lock_resp {
    camera_cmd_hdr_t hdr;
    zx_status_t     result;
} camera_rb_cmd_frame_lock_resp_t;

// CAMERA_RB_CMD_FRAME_RELEASE
//
// Unlocks the specified frame, allowing the driver to reuse the memory.
typedef struct camera_rb_cmd_frame_release_req {
    camera_cmd_hdr_t hdr;
    // This is from the camera metadata buffer.
    uint32_t frame_number;
} camera_rb_cmd_frame_release_req_t;

typedef struct camera_rb_cmd_frame_release_resp {
    camera_cmd_hdr_t hdr;
    zx_status_t     result;
} camera_rb_cmd_frame_release_resp_t;

// CAMERA_RB_METADATA_POSITION_NOTIFY
#define CAMERA_RB_METADATA_POSITION_NOTIFY_MAX_ENTRIES (8u)

// Sent by the driver to the client when one or more frames are
// available for processing.
typedef struct camera_rb_metadata_position_notify {
    camera_cmd_hdr_t hdr;

    // The frame numbers for the metadata and video data.
    // The client should issue a CAMERA_RB_CMD_FRAME_LOCK request with
    // the relevant frame number before reading the corresponding metadata entry.
    // The length of this array is equal to the number of metadata entries.
    uint32_t frame_numbers[CAMERA_RB_METADATA_POSITION_NOTIFY_MAX_ENTRIES];
    // The current position (in bytes) of the driver/hardware's
    // pointer in the metadata ring buffer.
    uint32_t metadata_buffer_pos;
} camera_rb_metadata_position_notify_t;

__END_CDECLS
