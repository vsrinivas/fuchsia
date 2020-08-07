// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
constexpr int kInputFileFrameCount = 30;

const char* kGoldenSha256 = "a4418265eaa493604731d6871523ac2a0d606f40cddd48e2a8cd0b0aa5f152e1";

}  // namespace

int main(int argc, char* argv[]) {
  // TODO(fxb/13483): The retries should not be necessary here.  These are presently needed to
  // de-flake due to a decode correctness bug that results in a few slightly incorrect pixels
  // sometimes.
  constexpr uint32_t kMaxRetryCount = 100;
  for (uint32_t try_ordinal = 0; try_ordinal < kMaxRetryCount; ++try_ordinal) {
    if (0 == use_video_decoder_test(kInputFilePath, kInputFileFrameCount, use_h264_multi_decoder,
                                /*is_secure_output=*/false, /*is_secure_input=*/false,
                                /*min_output_buffer_count=*/0, kGoldenSha256)) {
      if (try_ordinal != 0) {
        LOGF("WARNING - fxb/13483 - internal de-flaking used - extra attempt count: %u", try_ordinal);
      }
      return 0;
    }
    LOGF("WARNING - fxb/13483 - decode may have flaked - internally de-flaking (for now)");
  }
  LOGF("Incorrect hash seen every time despite de-flaking retries.  FAIL");
  return -1;
}
