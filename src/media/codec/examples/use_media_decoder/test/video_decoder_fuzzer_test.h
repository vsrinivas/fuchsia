// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_TEST_VIDEO_DECODER_FUZZER_TEST_H_
#define SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_TEST_VIDEO_DECODER_FUZZER_TEST_H_
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/media/codec_impl/fourcc.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/logger.h>
#include <stdio.h>
#include <stdlib.h>

#include <map>
#include <random>
#include <set>

#include "../in_stream_file.h"
#include "../in_stream_peeker.h"
#include "../input_copier.h"
#include "../use_video_decoder.h"
#include "../util.h"
#include "src/lib/fxl/command_line.h"
#include "use_video_decoder_test.h"

int video_fuzzer_test(std::string input_file_path, UseVideoDecoderFunction use_video_decoder,
                      uint32_t iteration_count, fxl::CommandLine command_line);

int run_fuzzer_test_instance_for_offset(std::string input_file_path,
                                        UseVideoDecoderFunction use_video_decoder,
                                        uint32_t stream_offset, uint8_t modified_value);

#endif  // SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_TEST_VIDEO_DECODER_FUZZER_TEST_H_
