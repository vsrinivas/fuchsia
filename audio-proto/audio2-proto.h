// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/device/audio2.h>

namespace audio {
namespace audio2_proto {

// C++ style aliases for protocol structures and types.
using Cmd    = audio2_cmd_t;
using CmdHdr = audio2_cmd_hdr_t;

// AUDIO2_STREAM_CMD_SET_FORMAT
using SampleFormat     = audio2_sample_format_t;
using StreamSetFmtReq  = audio2_stream_cmd_set_format_req_t;
using StreamSetFmtResp = audio2_stream_cmd_set_format_resp_t;

// AUDIO2_STREAM_CMD_GET_GAIN
using GetGainReq  = audio2_stream_cmd_get_gain_req;
using GetGainResp = audio2_stream_cmd_get_gain_resp;

// AUDIO2_STREAM_CMD_SET_GAIN
using SetGainReq  = audio2_stream_cmd_set_gain_req;
using SetGainResp = audio2_stream_cmd_set_gain_resp;

// AUDIO2_STREAM_CMD_PLUG_DETECT
using PlugDetectReq  = audio2_stream_cmd_plug_detect_req_t;
using PlugDetectResp = audio2_stream_cmd_plug_detect_resp_t;

// AUDIO2_STREAM_PLUG_DETECT_NOTIFY
using PlugDetectNotify = audio2_stream_plug_detect_notify_t;

// AUDIO2_RB_CMD_GET_FIFO_DEPTH
using RingBufGetFifoDepthReq  = audio2_rb_cmd_get_fifo_depth_req_t;
using RingBufGetFifoDepthResp = audio2_rb_cmd_get_fifo_depth_resp_t;

// AUDIO2_RB_CMD_GET_BUFFER
using RingBufGetBufferReq  = audio2_rb_cmd_get_buffer_req_t;
using RingBufGetBufferResp = audio2_rb_cmd_get_buffer_resp_t;

// AUDIO2_RB_CMD_START
using RingBufStartReq  = audio2_rb_cmd_start_req_t;
using RingBufStartResp = audio2_rb_cmd_start_resp_t;

// AUDIO2_RB_CMD_STOP
using RingBufStopReq  = audio2_rb_cmd_stop_req_t;
using RingBufStopResp = audio2_rb_cmd_stop_resp_t;

// AUDIO2_RB_POSITION_NOTIFY
using RingBufPositionNotify = audio2_rb_position_notify_t;

const char* SampleFormatToString(SampleFormat sample_format);

}  // namespace audio2_proto
}  // namespace audio
