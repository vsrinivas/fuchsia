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
#include <stdio.h>
#include <stdlib.h>

#include <map>
#include <set>

#include "../use_video_decoder.h"
#include "../util.h"
#include "src/lib/fxl/logging.h"
#include "use_video_decoder_test.h"

namespace {

constexpr char kInputFilePath[] = "/pkg/data/test-25fps.vp9.ivf";
constexpr int kInputFileFrameCount = 250;

const char* kGoldenSha256 = "7af41ec1056227e4c83459240c89db07916d8b67d31d023260a0895bc1fc511f";

}  // namespace

// Test vp9 decoder's ability to skip frames until keyframe when input starts at non-keyframe.  This
// is especially relevant to any decoder that has an internal watchdog that might reset decoder
// stream state at any arbitrary frame.
int main(int argc, char* argv[]) {
  UseVideoDecoderTestParams test_params;
  test_params.Add("skip_frame_ordinal", 0);
  test_params.Add("first_expected_output_frame_ordinal", 150);
  return use_video_decoder_test(kInputFilePath, kInputFileFrameCount, use_vp9_decoder,
                                /*is_secure_output=*/false, /*is_secure_input=*/false,
                                /*min_output_buffer_count=*/0, kGoldenSha256, &test_params);
}
