// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/shared/device_id.h"

namespace media::audio {

std::string DeviceUniqueIdToString(const audio_stream_unique_id_t& id) {
  static_assert(sizeof(id.data) == 16, "Unexpected unique ID size");
  char buf[(sizeof(id.data) * 2) + 1];

  const auto& d = id.data;
  snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
           d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8], d[9], d[10], d[11], d[12], d[13],
           d[14], d[15]);
  return std::string(buf, sizeof(buf) - 1);
}

fpromise::result<audio_stream_unique_id_t> DeviceUniqueIdFromString(const std::string& id) {
  if (id.size() != 32) {
    return fpromise::error();
  }

  audio_stream_unique_id_t unique_id;
  auto& d = unique_id.data;
  const auto captured =
      sscanf(id.c_str(),
             "%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx",
             &d[0], &d[1], &d[2], &d[3], &d[4], &d[5], &d[6], &d[7], &d[8], &d[9], &d[10], &d[11],
             &d[12], &d[13], &d[14], &d[15]);
  if (captured != 16) {
    return fpromise::error();
  }

  return fpromise::ok(unique_id);
}

}  // namespace media::audio
