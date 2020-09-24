// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_TEST_DECODER_CONFORMANCE_TEST_H_
#define SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_TEST_DECODER_CONFORMANCE_TEST_H_

#include "../use_video_decoder.h"
#include "use_video_decoder_test.h"

[[nodiscard]] int decoder_conformance_test(int argc, char* argv[],
                                           UseVideoDecoderFunction use_video_decoder,
                                           const char* input_file_path, const char* md5_file_path);

#endif  // SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_TEST_DECODER_CONFORMANCE_TEST_H_
