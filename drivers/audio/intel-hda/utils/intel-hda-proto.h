// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/device/intel-hda.h>

namespace audio {
namespace ihda_proto {

// C++ style aliases for codec request/response structures
using Cmd               = ihda_cmd_t;
using CmdHdr            = ihda_cmd_hdr_t;
using GetIDsReq         = ihda_get_ids_req_t;
using GetIDsResp        = ihda_get_ids_resp_t;
using SendCORBCmdReq    = ihda_codec_send_corb_cmd_req_t;
using SendCORBCmdResp   = ihda_codec_send_corb_cmd_resp_t;
using RequestStreamReq  = ihda_codec_request_stream_req_t;
using RequestStreamResp = ihda_codec_request_stream_resp_t;
using ReleaseStreamReq  = ihda_codec_release_stream_req_t;
using ReleaseStreamResp = ihda_codec_release_stream_resp_t;
using SetStreamFmtReq   = ihda_codec_set_stream_format_req_t;
using SetStreamFmtResp  = ihda_codec_set_stream_format_resp_t;

}  // ihda_proto
}  // namespace audio
