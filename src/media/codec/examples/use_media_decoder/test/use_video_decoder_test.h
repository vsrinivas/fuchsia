// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_TEST_USE_VIDEO_DECODER_TEST_H_
#define SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_TEST_USE_VIDEO_DECODER_TEST_H_

#include <lib/sys/cpp/component_context.h>

#include "src/media/codec/examples/use_media_decoder/in_stream_peeker.h"
#include "src/media/codec/examples/use_media_decoder/use_video_decoder.h"

// For tests that just want to decode an input file with a known number of
// frames.
int use_video_decoder_test(std::string input_file_path, int expected_frame_count,
                           UseVideoDecoderFunction use_video_decoder, bool is_secure_output,
                           bool is_secure_input, uint32_t min_output_buffer_count,
                           std::string golden_sha256);

// For tests that want to provide their own InStreamPeeker and EmitFrame.
bool decode_video_stream_test(async::Loop* fidl_loop, thrd_t fidl_thread,
                              sys::ComponentContext* component_context,
                              InStreamPeeker* in_stream_peeker,
                              UseVideoDecoderFunction use_video_decoder,
                              uint64_t min_output_buffer_size, uint32_t min_output_buffer_count,
                              bool is_secure_output, bool is_secure_input, EmitFrame emit_frame);

#endif  // SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_TEST_USE_VIDEO_DECODER_TEST_H_
