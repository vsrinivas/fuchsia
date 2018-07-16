// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_MEDIA_SIMPLE_SINE_SYNC_SIMPLE_SINE_SYNC_H_
#define GARNET_EXAMPLES_MEDIA_SIMPLE_SINE_SYNC_SIMPLE_SINE_SYNC_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/vmo-utils/vmo_mapper.h>

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
  void set_float(bool enable_float) { use_float_ = enable_float; }

  int Run();

 private:
  bool AcquireRenderer();
  void SetMediaType();

  zx_status_t CreateMemoryMapping();

  void WriteAudioIntoBuffer(void* buffer, size_t num_frames);

  bool RefillBuffer();

  fuchsia::media::AudioPacket CreateAudioPacket(size_t payload_num);
  bool SendAudioPacket(fuchsia::media::AudioPacket packet);

  void WaitForPackets(size_t num_packets);

  fuchsia::media::AudioRenderer2SyncPtr audio_renderer_;

  vmo_utils::VmoMapper payload_buffer_;
  size_t sample_size_;
  size_t payload_size_;
  size_t total_mapping_size_;
  size_t num_packets_sent_ = 0u;
  zx_time_t start_time_;
  bool start_time_known_ = false;

  bool verbose_ = false;
  zx_duration_t low_water_mark_;
  zx_duration_t high_water_mark_;
  bool use_float_ = false;
};

}  // namespace examples

#endif  // GARNET_EXAMPLES_MEDIA_SIMPLE_SINE_SYNC_SIMPLE_SINE_SYNC_H_
