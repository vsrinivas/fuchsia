// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This manual test is a basic integration test of the codec_factory + device specific codec
// implementation.
//
// If this test breaks and it's not immediately obvious why, please feel free to
// involve dustingreen@ (me) in figuring it out.

#include <stdio.h>
#include <stdlib.h>

#include "decoder_conformance_test.h"
#include "use_video_decoder_test.h"

namespace {

// The actual contents of these files will vary per test package.  See files
// under prebuilt/test_data/media/third_party/webm_vp9_conformance_streams.
constexpr char kInputFilePath[] = "/pkg/data/vp9.ivf";
constexpr char kI420Md5FilePath[] = "/pkg/data/vp9.md5";

}  // namespace

int main(int argc, char* argv[]) {
  return decoder_conformance_test(argc, argv, use_vp9_decoder, kInputFilePath, kI420Md5FilePath);
}
