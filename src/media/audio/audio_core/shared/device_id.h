// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_DEVICE_ID_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_DEVICE_ID_H_

#include <lib/fpromise/promise.h>
#include <zircon/device/audio.h>

#include <string>

namespace media::audio {

std::string DeviceUniqueIdToString(const audio_stream_unique_id_t& id);
fpromise::result<audio_stream_unique_id_t> DeviceUniqueIdFromString(const std::string& unique_id);

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_DEVICE_ID_H_
