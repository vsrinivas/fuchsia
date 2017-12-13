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
    RGB32         // 32bpp BGRA, 1 plane.
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

__END_CDECLS
