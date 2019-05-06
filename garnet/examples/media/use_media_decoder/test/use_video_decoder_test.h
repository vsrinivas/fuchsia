// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../use_video_decoder.h"

int use_video_decoder_test(
    const char* input_file_path, int frame_count,
    UseVideoDecoderFunction use_video_decoder,
    const std::map<uint32_t, const char*>& golden_sha256s);
