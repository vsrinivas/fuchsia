// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "video_decoder_fuzzer_test.h"

namespace {

constexpr char kInputFilePath[] = "/pkg/data/bear-vp9.ivf";

}  // namespace

int main(int argc, char* argv[]) {
  return video_fuzzer_test(kInputFilePath, use_vp9_decoder, 100,
                           fxl::CommandLineFromArgcArgv(argc, argv));
}
