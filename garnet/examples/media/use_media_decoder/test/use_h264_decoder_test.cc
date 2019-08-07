// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This manual test is a basic integration test of the codec_factory +
// amlogic_video_decoder driver.
//
// If this test breaks and it's not immediately obvoius why, please feel free to
// involve dustingreen@ (me) in figuring it out.

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/media/codec_impl/fourcc.h>
#include <src/lib/fxl/logging.h>
#include <stdio.h>
#include <stdlib.h>

#include <map>
#include <set>

#include "../use_video_decoder.h"
#include "../util.h"
#include "use_video_decoder_test.h"

namespace {

constexpr char kInputFilePath[] = "/pkg/data/bear.h264";
constexpr int kInputFileFrameCount = 30;

const char* kGoldenSha256 = "a4418265eaa493604731d6871523ac2a0d606f40cddd48e2a8cd0b0aa5f152e1";

}  // namespace

int main(int argc, char* argv[]) {
  return use_video_decoder_test(kInputFilePath, kInputFileFrameCount,
                                use_h264_decoder, kGoldenSha256);
}
