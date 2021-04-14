// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_TESTS_INTEGRATION_VIDEO_FRAME_HELPERS_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_TESTS_INTEGRATION_VIDEO_FRAME_HELPERS_H_

#include <string>

#include <openssl/sha.h>

#include "video_frame.h"

namespace amlogic_decoder {
namespace test {

void HashFrame(VideoFrame* frame, uint8_t digest[SHA256_DIGEST_LENGTH]);

std::string StringifyHash(uint8_t digest[SHA256_DIGEST_LENGTH]);
std::string CppStringifyHash(uint8_t digest[SHA256_DIGEST_LENGTH]);

}  // namespace test
}  // namespace amlogic_decoder

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_TESTS_INTEGRATION_VIDEO_FRAME_HELPERS_H_
