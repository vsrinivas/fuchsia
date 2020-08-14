// Copyright 2020 The Fuchsia Authors. All rights reserved.
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
#include <thread>

#include "../use_video_decoder.h"
#include "../util.h"
#include "lib/zx/time.h"
#include "use_video_decoder_test.h"

namespace {

constexpr char kInputFilePath[] = "/pkg/data/bear.h264";
constexpr int kInputFileFrameCount = 300;

const char* kGoldenSha256 = "1cc5002b6d68f34f601813c5bbed775eb1a1033a8109bdf2fed59160f2edc207";

constexpr uint32_t kThreadCount = 2;

}  // namespace

int main(int argc, char* argv[]) {
  // TODO(fxbug.dev/13483): The retries should not be necessary here.  These are presently needed to
  // de-flake due to a decode correctness bug that results in a few slightly incorrect pixels
  // sometimes.
  constexpr uint32_t kMaxRetryCount = 100;
  for (uint32_t try_ordinal = 0; try_ordinal < kMaxRetryCount; ++try_ordinal) {
    const UseVideoDecoderTestParams test_params{
        .keep_stream_modulo = 4,
        .loop_stream_count = 20,
        .mime_type = "video/h264-multi",
    };
    int result[kThreadCount] = {};
    std::atomic_bool go = false;
    std::unique_ptr<std::thread> threads[kThreadCount];
    for (uint32_t i = 0; i < kThreadCount; ++i) {
      threads[i] = std::make_unique<std::thread>([&result, i, &go, &test_params] {
        while (!go) {
          zx::nanosleep(zx::deadline_after(zx::usec(1)));
        }
        result[i] =
            use_video_decoder_test(kInputFilePath, kInputFileFrameCount, use_h264_decoder,
                                  /*is_secure_output=*/false, /*is_secure_input=*/false,
                                  /*min_output_buffer_count=*/0, kGoldenSha256, &test_params);
      });
    }
    go = true;
    for (uint32_t i = 0; i < kThreadCount; ++i) {
      threads[i]->join();
    }
    bool failed = false;
    for (uint32_t i = 0; i < kThreadCount; ++i) {
      if (result[i] != 0) {
        LOGF("Sub-test failed: %u", i);
        failed = true;
      } else {
        LOGF("Sub-test passed: %u", i);
      }
    }
    if (failed) {
      LOGF("At least one sub-test failed, so fail.");
      LOGF("WARNING - fxb/13483 - decode may have flaked - internally de-flaking (for now)");
      continue;
    } else {
      if (try_ordinal != 0) {
        LOGF("WARNING - fxb/13483 - internal de-flaking used - extra attempt count: %u", try_ordinal);
      }
      LOGF("Both streams worked.  PASS");
      return 0;
    }
    LOGF("WARNING - fxb/13483 - decode may have flaked - internally de-flaking (for now)");
  }
  LOGF("Incorrect hash seen every time despite de-flaking retries.  FAIL");
  return -1;
}
