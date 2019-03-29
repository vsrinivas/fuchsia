// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/audio_output.h"

#include "garnet/bin/media/audio_core/audio_device_manager.h"
#include "garnet/bin/media/audio_core/audio_link.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/time/time_delta.h"

namespace media::audio {

AudioOutput::AudioOutput(AudioDeviceManager* manager)
    : AudioDevice(Type::Output, manager) {}

}  // namespace media::audio
