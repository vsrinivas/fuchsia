// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This manual test is a basic integration test of the codec_factory +
// amlogic_video_decoder driver.
//
// If this test breaks and it's not immediately obvoius why, please feel free to
// involve dustingreen@ (me) in figuring it out.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/media/codec_impl/fourcc.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <stdio.h>
#include <stdlib.h>

#include <map>
#include <set>

#include <openssl/md5.h>

#include "../in_stream_file.h"
#include "../in_stream_http.h"
#include "../in_stream_peeker.h"
#include "../use_video_decoder.h"
#include "../util.h"
#include "decoder_conformance_test.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "use_video_decoder_test.h"

namespace {

// The actual contents of these files will vary per test package.  See files
// under garnet/test_data/media/third_party/webm_vp9_conformance_streams.
constexpr char kInputFilePath[] = "/pkg/data/vp9.ivf";
constexpr char kI420Md5FilePath[] = "/pkg/data/vp9.md5";

}  // namespace

int main(int argc, char* argv[]) {
  return decoder_conformance_test(argc, argv, use_vp9_decoder, kInputFilePath, kI420Md5FilePath);
}
