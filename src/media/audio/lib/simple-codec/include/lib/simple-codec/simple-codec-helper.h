// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_HELPER_H_
#define SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_HELPER_H_

#include <lib/simple-codec/simple-codec-types.h>

#include <string>
#include <vector>

namespace audio {
// Checks if a format is part of the provided supported formats.
bool IsDaiFormatSupported(const DaiFormat& format,
                          const std::vector<DaiSupportedFormats>& supported);
bool IsDaiFormatSupported(const DaiFormat& format, const DaiSupportedFormats& supported);
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_HELPER_H_
