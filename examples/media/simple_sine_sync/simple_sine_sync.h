// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/media/fidl/media_renderer.fidl.h"
#include "lib/media/fidl/media_transport.fidl.h"
// #include "lib/media/timeline/timeline.h"

namespace examples {

class MediaApp {
 public:
  MediaApp();
  ~MediaApp();

  void set_verbose(bool verbose) { verbose_ = verbose; }
  void set_first_pts_delay_ms(int64_t value) {
    first_pts_delay_ = ZX_MSEC(value);
  }
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
  void WriteStereoAudioIntoBuffer(uintptr_t buffer, size_t num_frames);

  bool RefillBuffer();

  media::MediaPacketPtr CreateMediaPacket(size_t payload_num);
  bool SendMediaPacket(media::MediaPacketPtr packet);

  bool StartPlayback(zx_time_t reference_time);
  void WaitForPackets(size_t num_packets);

  media::MediaRendererSyncPtr media_renderer_;
  media::MediaPacketConsumerSyncPtr packet_consumer_;
  media::MediaTimelineControlPointSyncPtr timeline_control_point_;

  zx::vmo vmo_;
  uintptr_t mapped_address_ = 0u;
  size_t num_packets_sent_ = 0u;
  zx_time_t start_time_;

  bool verbose_ = false;
  zx_duration_t first_pts_delay_;
  zx_duration_t low_water_mark_;
  zx_duration_t high_water_mark_;
};

}  // namespace examples
