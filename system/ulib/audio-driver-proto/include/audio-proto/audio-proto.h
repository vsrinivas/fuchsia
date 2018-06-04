// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/device/audio.h>

namespace audio {
namespace audio_proto {

// C++ style aliases for protocol structures and types.
using Cmd    = audio_cmd_t;
using CmdHdr = audio_cmd_hdr_t;

// Structures used with GET/SET format
using SampleFormat = audio_sample_format_t;
using FormatRange  = audio_stream_format_range_t;

// AUDIO_STREAM_CMD_GET_FORMATS
using StreamGetFmtsReq  = audio_stream_cmd_get_formats_req_t;
using StreamGetFmtsResp = audio_stream_cmd_get_formats_resp_t;

// AUDIO_STREAM_CMD_SET_FORMAT
using StreamSetFmtReq  = audio_stream_cmd_set_format_req_t;
using StreamSetFmtResp = audio_stream_cmd_set_format_resp_t;

// AUDIO_STREAM_CMD_GET_GAIN
using GetGainReq  = audio_stream_cmd_get_gain_req;
using GetGainResp = audio_stream_cmd_get_gain_resp;

// AUDIO_STREAM_CMD_SET_GAIN
using SetGainReq  = audio_stream_cmd_set_gain_req;
using SetGainResp = audio_stream_cmd_set_gain_resp;

// AUDIO_STREAM_CMD_PLUG_DETECT
using PlugDetectReq  = audio_stream_cmd_plug_detect_req_t;
using PlugDetectResp = audio_stream_cmd_plug_detect_resp_t;

// AUDIO_STREAM_PLUG_DETECT_NOTIFY
using PlugDetectNotify = audio_stream_plug_detect_notify_t;

// AUDIO_STREAM_CMD_GET_UNIQUE_ID
using GetUniqueIdReq = audio_stream_cmd_get_unique_id_req_t;
using GetUniqueIdResp = audio_stream_cmd_get_unique_id_resp_t;

// AUDIO_STREAM_CMD_GET_STRING
using GetStringReq = audio_stream_cmd_get_string_req_t;
using GetStringResp = audio_stream_cmd_get_string_resp_t;

// AUDIO_RB_CMD_GET_FIFO_DEPTH
using RingBufGetFifoDepthReq  = audio_rb_cmd_get_fifo_depth_req_t;
using RingBufGetFifoDepthResp = audio_rb_cmd_get_fifo_depth_resp_t;

// AUDIO_RB_CMD_GET_BUFFER
using RingBufGetBufferReq  = audio_rb_cmd_get_buffer_req_t;
using RingBufGetBufferResp = audio_rb_cmd_get_buffer_resp_t;

// AUDIO_RB_CMD_START
using RingBufStartReq  = audio_rb_cmd_start_req_t;
using RingBufStartResp = audio_rb_cmd_start_resp_t;

// AUDIO_RB_CMD_STOP
using RingBufStopReq  = audio_rb_cmd_stop_req_t;
using RingBufStopResp = audio_rb_cmd_stop_resp_t;

// AUDIO_RB_POSITION_NOTIFY
using RingBufPositionNotify = audio_rb_position_notify_t;

const char* SampleFormatToString(SampleFormat sample_format);

}  // namespace audio_proto
}  // namespace audio
