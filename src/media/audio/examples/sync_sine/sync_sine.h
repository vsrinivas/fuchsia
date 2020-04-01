// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_EXAMPLES_SYNC_SINE_SYNC_SINE_H_
#define SRC_MEDIA_AUDIO_EXAMPLES_SYNC_SINE_SYNC_SINE_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/clock.h>

namespace examples {

class MediaApp {
 public:
  MediaApp(std::unique_ptr<sys::ComponentContext> context);
  ~MediaApp();

  void set_verbose(bool verbose) { verbose_ = verbose; }
  void set_low_water_mark_from_ms(int64_t value) { low_water_mark_ = ZX_MSEC(value); }
  void set_high_water_mark_from_ms(int64_t value) { high_water_mark_ = ZX_MSEC(value); }
  void set_float(bool enable_float) { use_float_ = enable_float; }

  zx_status_t Run();

 private:
  zx_status_t AcquireAudioRendererSync();
  zx_status_t SetReferenceClock();
  zx_status_t SetStreamType();

  zx_status_t CreateMemoryMapping();

  void WriteAudioIntoBuffer(void* buffer, size_t num_frames);

  zx_status_t RefillBuffer();

  fuchsia::media::StreamPacket CreateAudioPacket(size_t payload_num);
  zx_status_t SendAudioPacket(fuchsia::media::StreamPacket packet);

  zx_status_t WaitForPackets(size_t num_packets);

  fuchsia::media::AudioRendererSyncPtr audio_renderer_sync_;

  std::unique_ptr<sys::ComponentContext> context_;
  fzl::VmoMapper payload_buffer_;
  size_t sample_size_;
  size_t payload_size_;
  size_t total_mapping_size_;
  size_t num_packets_sent_ = 0u;

  zx::clock reference_clock_;
  zx_time_t clock_start_time_;
  bool start_time_known_ = false;

  bool verbose_ = false;
  zx_duration_t low_water_mark_;
  zx_duration_t high_water_mark_;
  bool use_float_ = false;
};

}  // namespace examples

#endif  // SRC_MEDIA_AUDIO_EXAMPLES_SYNC_SINE_SYNC_SINE_H_
