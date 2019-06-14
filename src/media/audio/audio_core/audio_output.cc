// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_output.h"

namespace media::audio {

AudioOutput::AudioOutput(AudioDeviceManager* manager)
    : AudioDevice(Type::Output, manager) {}

}  // namespace media::audio
