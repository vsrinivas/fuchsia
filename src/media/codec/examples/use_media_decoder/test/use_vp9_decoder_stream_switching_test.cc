// Copyright 2018 The Fuchsia Authors. All rights reserved.
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

#include "../use_video_decoder.h"
#include "../util.h"
#include "use_video_decoder_test.h"

namespace {

constexpr char kInputFilePath[] = "/pkg/data/bear-vp9.ivf";
constexpr int kInputFileFrameCount = 1344;

const char* kGoldenSha256 = "a7f3f7c660574db37118f63503b7c0f2ff789d984dbd7b6d68ade4d284e7b42c";

}  // namespace

int main(int argc, char* argv[]) {
  const UseVideoDecoderTestParams test_params{
      // Processing only 21 frames per stream allows for switching streams more times within the
      // same wall clock test duration.
      .input_stop_stream_after_frame_ordinal = 20,
      .keep_stream_modulo = 4,
      .loop_stream_count = 127,
      .golden_sha256 = kGoldenSha256,
  };
  return use_video_decoder_test(kInputFilePath, kInputFileFrameCount, use_vp9_decoder,
                                /*is_secure_output=*/false, /*is_secure_input=*/false,
                                /*min_output_buffer_count=*/0, &test_params);
}
