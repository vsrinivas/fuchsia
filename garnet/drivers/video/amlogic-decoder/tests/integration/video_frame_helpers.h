// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_TESTS_INTEGRATION_VIDEO_FRAME_HELPERS_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_TESTS_INTEGRATION_VIDEO_FRAME_HELPERS_H_

#include <string>

#include <openssl/sha.h>

#include "video_frame.h"

void HashFrame(VideoFrame* frame, uint8_t digest[SHA256_DIGEST_LENGTH]);

std::string StringifyHash(uint8_t digest[SHA256_DIGEST_LENGTH]);

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_TESTS_INTEGRATION_VIDEO_FRAME_HELPERS_H_
