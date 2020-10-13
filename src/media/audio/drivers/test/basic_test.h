// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_TEST_BASIC_TEST_H_
#define SRC_MEDIA_AUDIO_DRIVERS_TEST_BASIC_TEST_H_

#include <fuchsia/hardware/audio/cpp/fidl.h>
#include <fuchsia/media/cpp/fidl.h>
#include <zircon/device/audio.h>

#include "src/lib/fsl/io/device_watcher.h"
#include "src/media/audio/drivers/test/test_base.h"

namespace media::audio::drivers::test {

class BasicTest : public TestBase {
 protected:
  void SetUp() override;

  void RequestPlugDetect();
  void RequestStreamProperties();
  void RequestGain();
  void RequestSetGain();

 private:
  static constexpr size_t kUniqueIdLength = 16;

  fuchsia::hardware::audio::StreamProperties stream_props_;
  fuchsia::hardware::audio::GainState gain_state_;
  fuchsia::hardware::audio::GainState set_gain_state_;
  fuchsia::hardware::audio::PlugState plug_state_;
  bool received_get_stream_properties_ = false;
  bool received_get_gain_ = false;
  bool received_plug_detect_ = false;
  bool issued_set_gain_ = false;
};

}  // namespace media::audio::drivers::test

#endif  // SRC_MEDIA_AUDIO_DRIVERS_TEST_BASIC_TEST_H_
