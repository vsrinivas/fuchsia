// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/vmo_mapper.h>

#include "lib/media/fidl/audio_renderer.fidl.h"
#include "lib/media/fidl/media_transport.fidl.h"

namespace examples {

class MediaApp {
 public:
  MediaApp();
  ~MediaApp();

  void set_verbose(bool verbose) { verbose_ = verbose; }
  void set_low_water_mark_ms(int64_t value) {
    low_water_mark_ = ZX_MSEC(value);
  }
  void set_high_water_mark_ms(int64_t value) {
    high_water_mark_ = ZX_MSEC(value);
  }

  int Run();

 private:
  bool AcquireRenderer();
  void SetMediaType();

  zx_status_t CreateMemoryMapping();
  void WriteAudioIntoBuffer(void* buffer, size_t num_frames);

  bool RefillBuffer();

  media::AudioPacketPtr CreateAudioPacket(size_t payload_num);
  bool SendAudioPacket(media::AudioPacketPtr packet);

  void WaitForPackets(size_t num_packets);

  media::AudioRenderer2SyncPtr audio_renderer_;

  fbl::VmoMapper payload_buffer_;
  size_t num_packets_sent_ = 0u;
  zx_time_t start_time_;
  bool start_time_known_ = false;

  bool verbose_ = false;
  zx_duration_t low_water_mark_;
  zx_duration_t high_water_mark_;
};

}  // namespace examples
