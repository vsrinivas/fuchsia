// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_FAKE_PLUG_DETECTOR_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_FAKE_PLUG_DETECTOR_H_

#include "src/media/audio/audio_core/v1/plug_detector.h"

namespace media::audio::testing {

class FakePlugDetector : public PlugDetector {
 public:
  virtual ~FakePlugDetector() = default;

  // |media::audio::PlugDetector|
  zx_status_t Start(Observer o) override {
    observer_ = std::move(o);
    std::vector<Device>::iterator it;
    while ((it = pending_devices_.begin()) != pending_devices_.end()) {
      observer_(std::move(it->name), it->input, std::move(it->stream_config));
      pending_devices_.erase(it);
    }
    return ZX_OK;
  }
  void Stop() override { observer_ = nullptr; }

 private:
  Observer observer_;
  struct Device {
    std::string name;
    bool input;
    fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig> stream_config;
  };
  std::vector<Device> pending_devices_;
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_FAKE_PLUG_DETECTOR_H_
