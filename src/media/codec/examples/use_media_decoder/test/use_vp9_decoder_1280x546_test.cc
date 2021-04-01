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

constexpr char kInputFilePath[] = "/pkg/data/bear-1280x546-vp9.ivf";
constexpr int kInputFileFrameCount = 30;

const char* kGoldenSha256 = "9f7e6621a29e87c080ab8b589e2ab07a8ec28e38fe4ee0799404d92993efd9de";

}  // namespace

int main(int argc, char* argv[]) {
  UseVideoDecoderTestParams test_params = {
      .golden_sha256 = kGoldenSha256,
  };
  return use_video_decoder_test(kInputFilePath, kInputFileFrameCount, use_vp9_decoder,
                                /*is_secure_output=*/false, /*is_secure_input=*/false,
                                /*min_output_buffer_count=*/0, &test_params);
}
