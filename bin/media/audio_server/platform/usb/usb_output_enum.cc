// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/audio_server/platform/usb/usb_output_enum.h"

#include "apps/media/src/audio_server/platform/usb/usb_output.h"
#include "lib/ftl/files/eintr_wrapper.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/logging.h"

namespace media {
namespace audio {

UsbOutputEnum::UsbOutputEnum() {}

UsbOutputEnum::~UsbOutputEnum() {}

AudioOutputPtr UsbOutputEnum::GetDefaultOutput(AudioOutputManager* manager) {
  if (usb_audio_enum_.output_device_paths().empty()) {
    return AudioOutputPtr(nullptr);
  }

  return UsbOutput::Create(usb_audio_enum_.output_device_paths().front(),
                           manager);
}

}  // namespace audio
}  // namespace media
