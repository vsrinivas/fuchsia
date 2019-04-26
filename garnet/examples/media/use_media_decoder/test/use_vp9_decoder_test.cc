// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This manual test is a basic integration test of the codec_factory +
// amlogic_video_decoder driver.
//
// If this test breaks and it's not immediately obvoius why, please feel free to
// involve dustingreen@ (me) in figuring it out.

#include <stdio.h>
#include <stdlib.h>
#include <map>

#include "use_video_decoder_test.h"
#include "../use_video_decoder.h"
#include "../util.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <src/lib/fxl/logging.h>
#include <lib/media/codec_impl/fourcc.h>

#include <set>

namespace {

constexpr char kInputFilePath[] = "/pkg/data/bear-vp9.ivf";
constexpr int kInputFileFrameCount = 82;

const std::map<uint32_t, const char*> GoldenSha256s = {
    {make_fourcc('N', 'V', '1', '2'),
     "c63aeea743c1e20a7a62120a2343083370940ce4d6bca30068a686a6b7146410"}};
}  // namespace

int main(int argc, char* argv[]) {
  return use_video_decoder_test(kInputFilePath, kInputFileFrameCount, use_vp9_decoder, GoldenSha256s);
}
