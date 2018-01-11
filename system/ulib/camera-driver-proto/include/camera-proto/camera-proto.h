// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/device/camera.h>

namespace camera {
namespace camera_proto {

// C++ style aliases for protocol structures and types.
using Cmd    = camera_cmd_t;
using CmdHdr = camera_cmd_hdr_t;

// Structures used in GET_FORMATS and SEND_CAPTURE_REQUEST.
using CaptureType = camera_capture_type_t;
using PixelFormat = camera_pixel_format_t;
using VideoFormat = camera_video_format_t;

// Format of entries in the metadata ring buffer.
using Metadata = camera_metadata_t;

// CAMERA_STREAM_CMD_GET_FORMATS
using GetFormatsReq  = camera_stream_cmd_get_formats_req_t;
using GetFormatsResp = camera_stream_cmd_get_formats_resp_t;

// CAMERA_STREAM_CMD_SET_FORMAT_REQUEST
using SetFormatReq  = camera_stream_cmd_set_format_req_t;
using SetFormatResp = camera_stream_cmd_set_format_resp_t;

// CAMERA_RB_CMD_GET_DATA_BUFFER
using RingBufGetDataBufferReq  = camera_rb_cmd_get_data_buffer_req_t;
using RingBufGetDataBufferResp = camera_rb_cmd_get_data_buffer_resp_t;

// CAMERA_RB_CMD_GET_METADATA_BUFFER
using RingBufGetMetadataBufferReq  = camera_rb_cmd_get_metadata_buffer_req_t;
using RingBufGetMetadataBufferResp = camera_rb_cmd_get_metadata_buffer_resp_t;

// CAMERA_RB_CMD_START
using RingBufStartReq  = camera_rb_cmd_start_req_t;
using RingBufStartResp = camera_rb_cmd_start_resp_t;

// CAMERA_RB_CMD_STOP
using RingBufStopReq  = camera_rb_cmd_stop_req_t;
using RingBufStopResp = camera_rb_cmd_stop_resp_t;

// CAMERA_RB_FRAME_LOCK
using RingBufFrameLockReq = camera_rb_cmd_frame_lock_req_t;
using RingBufFrameLockResp = camera_rb_cmd_frame_lock_resp_t;

// CAMERA_RB_FRAME_RELEASE
using RingBufFrameReleaseReq = camera_rb_cmd_frame_release_req_t;
using RingBufFrameReleaseResp = camera_rb_cmd_frame_release_resp_t;

// CAMERA_RB_FRAME_READY
using RingBufMetadataPositionNotify = camera_rb_metadata_position_notify_t;

}  // namespace camera_proto
}  // namespace camera
