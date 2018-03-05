// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>

#define CAMERA_IOCTL_GET_CHANNEL IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_CAMERA, 0)
IOCTL_WRAPPER_OUT(ioctl_camera_get_channel, CAMERA_IOCTL_GET_CHANNEL, zx_handle_t);

__BEGIN_CDECLS

typedef enum camera_cmd {
    // Commands sent on the stream channel.
    CAMERA_STREAM_CMD_GET_FORMATS = 0x1000,
    CAMERA_STREAM_CMD_SET_FORMAT  = 0x1001,

    // Commands sent on the video buffer channel
    CAMERA_VB_CMD_SET_BUFFER      = 0x2000,
    CAMERA_VB_CMD_START           = 0x2001,
    CAMERA_VB_CMD_STOP            = 0x2002,
    CAMERA_VB_CMD_FRAME_RELEASE   = 0x2003,

    // Async notifications sent on the video buffer channel.
    CAMERA_VB_FRAME_NOTIFY        = 0x3000,
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

    I420,
    M420,
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
    // The width, in pixels, of the decoded video.
    uint16_t width;
    // The height, in pixels, of the decoded video.
    uint16_t height;
    // The number of bytes per line of video.
    uint32_t stride;
    // The number of bits per pixel used to specify color in the decoded video.
    uint8_t bits_per_pixel;
    camera_pixel_format_t pixel_format;
    // The frame rate is frames_per_sec_numerator / frames_per_sec_denominator.
    uint32_t frames_per_sec_numerator;
    uint32_t frames_per_sec_denominator;

    // To deal with temporal encoding we will need
    // uint32 frames_per_packet;
} camera_video_format_t;

// camera_metadata_t
//
// Describes the characteristics of the corresponding frame in the
// data buffer.
typedef struct camera_metadata {
    // The time at the midpoint of the capture operation, expressed in
    // nanoseconds with respect to the monotonic clock.
    // i.e. the average between the start and end times at which the sensor
    // captured the frame, *not* the time that the driver received the frame
    // from the hardware interconnect/interface.
    int64_t timestamp;

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
// Sent by the client to indicate desired stream characteristics.
typedef struct camera_stream_cmd_set_format_req {
    camera_cmd_hdr_t hdr;

    camera_video_format_t video_format;
} camera_stream_cmd_set_format_req_t;

typedef struct camera_stream_cmd_set_format_resp {
    camera_cmd_hdr_t hdr;
    zx_status_t result;

    uint32_t max_frame_size;

    // NOTE: Upon success, a channel used to control the video buffer will also
    // be returned.
} camera_stream_cmd_set_format_resp_t;

// CAMERA_VB_CMD_SET_BUFFER
//
// Set the data buffer for storing frames.
// TODO(jocelyndang): consider whether the buffer should be provided by the
// driver instead.
typedef struct camera_vb_cmd_set_buffer_req {
    camera_cmd_hdr_t hdr;

    // NOTE: The client must transfer a VMO handle for the data buffer
    // with read-write permissions. The size of the VMO should be an
    // integral multiple of max_frame_size returned in SET_FORMAT.
} camera_vb_cmd_set_buffer_req_t;

typedef struct camera_vb_cmd_set_buffer_resp {
    camera_cmd_hdr_t hdr;
    zx_status_t result;
} camera_vb_cmd_set_buffer_resp_t;

// CAMERA_VB_CMD_START
//
// Starts the streaming of frames.
typedef struct camera_vb_cmd_start_req {
    camera_cmd_hdr_t hdr;
} camera_vb_cmd_start_req_t;

typedef struct camera_vb_cmd_start_resp {
    camera_cmd_hdr_t hdr;
    zx_status_t     result;
} camera_vb_cmd_start_resp_t;

// CAMERA_VB_CMD_STOP
//
// Stops the streaming of frames.
typedef struct camera_vb_cmd_stop_req {
    camera_cmd_hdr_t hdr;
} camera_vb_cmd_stop_req_t;

typedef struct camera_vb_cmd_stop_resp {
    camera_cmd_hdr_t hdr;
    zx_status_t     result;
} camera_vb_cmd_stop_resp_t;

// CAMERA_VB_CMD_FRAME_RELEASE
//
// Unlocks the specified frame, allowing the driver to reuse the memory.
typedef struct camera_vb_cmd_frame_release_req {
    camera_cmd_hdr_t hdr;
    // The position (in bytes) of the start of the frame in the data buffer.
    // This is from the FRAME_NOTIFY message.
    uint64_t data_vb_offset;
} camera_vb_cmd_frame_release_req_t;

typedef struct camera_vb_cmd_frame_release_resp {
    camera_cmd_hdr_t hdr;
    zx_status_t     result;
} camera_vb_cmd_frame_release_resp_t;

typedef enum camera_error {
    CAMERA_ERROR_NONE = 0x0,
    // An error occurred during the production of a frame.
    // No data will be available in the data buffer corresponding to this
    // notification.
    CAMERA_ERROR_FRAME = 0x1,

    // No space was available in the data buffer, resulting in a dropped frame.
    CAMERA_ERROR_BUFFER_FULL = 0x2
} camera_error_t;

// Sent by the driver to the client when a frame is available for processing,
// or an error occurred.
typedef struct camera_vb_frame_notify {
    camera_cmd_hdr_t hdr;

    // Non zero if an error occurred.
    camera_error_t error;

    // Number of bytes in the frame.
    uint32_t frame_size;

    // The position (in bytes) of the start of the frame in the data buffer.
    // This is guaranteed to be a multiple of max_frame_size returned in
    // SET_FORMAT.
    uint64_t data_vb_offset;

    camera_metadata_t metadata;

    // NOTE: The frame will be not be reused by the driver until the client
    // calls FRAME_RELEASE with the frame's timestamp.
} camera_vb_frame_notify_t;

__END_CDECLS
