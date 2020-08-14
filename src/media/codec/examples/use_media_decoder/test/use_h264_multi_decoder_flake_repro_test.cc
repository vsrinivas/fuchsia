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

constexpr char kInputFilePath[] = "/pkg/data/bear.h264";
constexpr int kInputFileFrameCount = 16;

const char* kGoldenSha256 = "4ecf3233f87a6ba6d54d5f0085301114e2053d06e1e9dfaf155c71c07d918660";
// astro bad hash seen lots: "1642472070f1f6753dd9254324d9867e644cbe787bd53a9303b622b589f7b8ad"
// astro bad hash seen some: "15091f31c6911576255bc87f1f4def03bed94bfbc44796f59760bca30bcf2965"
// sherlock bad hash seen once: "3aea6b5d6ab02e8d3e80ec7bb9628049f1da7a792b820ca1deba5a75e7e391a3"
// sherlock bad hash seen once: "54b1de3bbb8bec34635a6b1ce6acb05aebb07aa7cb103403e6120f0c475f6e29"
// sherlock bad hash seen once: "ca1ed4fc6bd25da2e409099b9de8c036827f17c9ef39f99d7ecdc92ab549605d"

// Repro rate on sherlock seems lower than on astro.

}  // namespace

int main(int argc, char* argv[]) {
  UseVideoDecoderTestParams test_params = {
      .frame_count = 16,
      // This uses h264-multi, but sets a test hook flag that forces context switching to occur
      // every time context switching is possible.
      //
      // TODO(fxbug.dev/13483): Plumb the ability to request special test flags via a different service to
      // reduce which clients can set test flags.
      .mime_type = "video/h264-multi/test/force-context-save-restore",
  };
  uint32_t good_count = 0;
  uint32_t bad_count = 0;
  for (uint32_t i = 0; i < 500; ++i) {
    int result = use_video_decoder_test(kInputFilePath, kInputFileFrameCount, use_h264_decoder,
                                        /*is_secure_output=*/false, /*is_secure_input=*/false,
                                        /*min_output_buffer_count=*/0, kGoldenSha256, &test_params);
    if (result != 0) {
      ++bad_count;
    } else {
      ++good_count;
    }
  }
  fprintf(stderr, "good_count: %u bad_count: %u\n", good_count, bad_count);
  if (bad_count != 0) {
    return -1;
  }
  return 0;
}
