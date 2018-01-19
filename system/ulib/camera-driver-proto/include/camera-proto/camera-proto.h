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

// Structures used in GET_FORMATS and SET_FORMAT.
using CaptureType = camera_capture_type_t;
using PixelFormat = camera_pixel_format_t;
using VideoFormat = camera_video_format_t;

// Format of frame metadata.
using Metadata = camera_metadata_t;

// CAMERA_STREAM_CMD_GET_FORMATS
using GetFormatsReq  = camera_stream_cmd_get_formats_req_t;
using GetFormatsResp = camera_stream_cmd_get_formats_resp_t;

// CAMERA_STREAM_CMD_SET_FORMAT
using SetFormatReq  = camera_stream_cmd_set_format_req_t;
using SetFormatResp = camera_stream_cmd_set_format_resp_t;

// CAMERA_VB_CMD_SET_BUFFER
using VideoBufSetBufferReq = camera_vb_cmd_set_buffer_req_t;
using VideoBufSetBufferResp = camera_vb_cmd_set_buffer_resp_t;

// CAMERA_VB_CMD_START
using VideoBufStartReq  = camera_vb_cmd_start_req_t;
using VideoBufStartResp = camera_vb_cmd_start_resp_t;

// CAMERA_VB_CMD_STOP
using VideoBufStopReq  = camera_vb_cmd_stop_req_t;
using VideoBufStopResp = camera_vb_cmd_stop_resp_t;

// CAMERA_VB_FRAME_RELEASE
using VideoBufFrameReleaseReq = camera_vb_cmd_frame_release_req_t;
using VideoBufFrameReleaseResp = camera_vb_cmd_frame_release_resp_t;

// CAMERA_VB_FRAME_NOTIFY
using VideoBufFrameNotify = camera_vb_frame_notify_t;

}  // namespace camera_proto
}  // namespace camera
