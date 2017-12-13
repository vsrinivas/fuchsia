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

// CAMERA_STREAM_CMD_GET_FORMATS
using GetFormatsReq  = camera_stream_cmd_get_formats_req_t;
using GetFormatsResp = camera_stream_cmd_get_formats_resp_t;

// CAMERA_STREAM_CMD_SET_FORMAT_REQUEST
using SetFormatReq  = camera_stream_cmd_set_format_req_t;
using SetFormatResp = camera_stream_cmd_set_format_resp_t;

}  // namespace camera_proto
}  // namespace camera
