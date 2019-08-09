// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_AUDIO_CORE_TEST_BASE_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_AUDIO_CORE_TEST_BASE_H_

#include <fuchsia/media/cpp/fidl.h>

#include "src/media/audio/lib/test/audio_test_base.h"
#include "src/media/audio/lib/test/constants.h"

namespace media::audio::test {

//
// AudioCoreTestBase
//
// These tests verify asynchronous usage of audio_core protocols, in a non-hermetic environment.
// TODO(mpuryear): if we don't anticipate creating non-hermetic tests, eliminate AudioTestBase and
// AudioCoreTestBase, and combine constants.h into hermetic_audio_test.h.
class AudioCoreTestBase : public AudioTestBase {
 protected:
  void SetUp() override;
  void TearDown() override;

  void ExpectCallback() override;
  void ExpectDisconnect() override;

  fuchsia::media::AudioCorePtr audio_core_;
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_AUDIO_CORE_TEST_BASE_H_
