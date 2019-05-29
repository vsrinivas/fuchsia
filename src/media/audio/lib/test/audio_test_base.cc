// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/test/audio_test_base.h"

namespace media::audio::test {

std::unique_ptr<sys::ComponentContext> AudioTestBase::startup_context_;

//
// AudioTestBase implementation
//
void AudioTestBase::SetUpTestSuite() {
  TestFixture::SetUpTestSuite();

  if (!startup_context_) {
    startup_context_ = sys::ComponentContext::Create();
  }
}

}  // namespace media::audio::test
