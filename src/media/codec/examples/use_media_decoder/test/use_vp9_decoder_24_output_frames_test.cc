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

constexpr char kInputFilePath[] = "/pkg/data/bear-vp9.ivf";
constexpr int kInputFileFrameCount = 82;

constexpr uint32_t kMinOutputBufferCount = 24;

const char* kGoldenSha256 = "8317a8c078a0c27b7a524a25bf9964ee653063237698411361b415a449b23014";

}  // namespace

int main(int argc, char* argv[]) {
  return use_video_decoder_test(kInputFilePath, kInputFileFrameCount, use_vp9_decoder,
                                /*is_secure_output=*/false, /*is_secure_input=*/false,
                                kMinOutputBufferCount, kGoldenSha256);
}
