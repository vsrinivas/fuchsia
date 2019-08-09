// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_CONSTANTS_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_CONSTANTS_H_

#include <fuchsia/media/cpp/fidl.h>

namespace media::audio::test {

// TODO(mpuryear): move this (and mixer's duplicate) to gain_control.fidl
constexpr float kUnityGainDb = 0.0f;
constexpr float kTooLowGainDb = fuchsia::media::audio::MUTED_GAIN_DB - 0.1f;
constexpr float kTooHighGainDb = fuchsia::media::audio::MAX_GAIN_DB + 0.1f;

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_CONSTANTS_H_
