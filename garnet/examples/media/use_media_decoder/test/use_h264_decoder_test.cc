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

const std::map<uint32_t, const char*> GoldenSha256s = {
    {make_fourcc('Y', 'V', '1', '2'),
     "39e861466dede78e5be008f85dba53efcee23b7a064170e4c00361383e67690d"},
    // YV12 without SHA256_Update_VideoParameters():
    // f3116ef8cf0f69c3d9316246a3896f96684f513ce9664b9b55e195c964cc64a0
    {make_fourcc('N', 'V', '1', '2'),
     "2ab4b1f47636ac367b5cc0da2bf8d901a9e2b5db40126b50f5f75ee5b3b8c8df"}};
// NV12 without SHA256_Update_VideoParameters():
// 84ae3e279d8b85d3a3b10c06489d9ffb0a968d99baa498d20f28788c0090c1d5
}  // namespace

int main(int argc, char* argv[]) {
  return use_video_decoder_test(kInputFilePath, kInputFileFrameCount,
                                use_h264_decoder, GoldenSha256s);
}
