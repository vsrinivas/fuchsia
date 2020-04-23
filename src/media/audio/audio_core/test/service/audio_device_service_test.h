// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_SERVICE_AUDIO_DEVICE_SERVICE_TEST_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_SERVICE_AUDIO_DEVICE_SERVICE_TEST_H_

#include <fuchsia/media/cpp/fidl.h>

#include "src/media/audio/audio_core/testing/fake_audio_driver.h"
#include "src/media/audio/lib/test/hermetic_audio_test.h"

namespace media::audio::test {

class AudioDeviceServiceTest : public HermeticAudioTest,
                               public ::testing::WithParamInterface<uint8_t> {
 protected:
  void SetUp() override;
  void TearDown() override;

  const std::vector<fuchsia::media::AudioDeviceInfo>& devices() { return devices_; }
  fuchsia::media::AudioDeviceEnumerator& audio_device_enumerator() {
    return *audio_device_enumerator_.get();
  }

  uint64_t device_token() { return device_token_; }
  void set_device_token(uint64_t token) { device_token_ = token; }

 private:
  fuchsia::media::AudioDeviceEnumeratorPtr audio_device_enumerator_;
  std::vector<fuchsia::media::AudioDeviceInfo> devices_;
  uint64_t device_token_;

  std::unique_ptr<testing::FakeAudioDriver> driver_;
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_SERVICE_AUDIO_DEVICE_SERVICE_TEST_H_
