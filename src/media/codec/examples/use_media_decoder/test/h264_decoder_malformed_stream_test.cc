// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "video_decoder_fuzzer_test.h"

namespace {

constexpr char kInputFilePath[] = "/pkg/data/bear.h264";

}  // namespace

TEST(H264Malformed, MissingCurrentFrame) {
  // Values determined from running h264_decoder_fuzzer_test.
  int result = run_fuzzer_test_instance_for_offset(kInputFilePath, use_h264_decoder, 621, 93);
  EXPECT_EQ(0, result);
}
