// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

#include "garnet/lib/media/audio_output_device.h"
#include "lib/media/c/audio.h"
#include "lib/media/fidl/media_renderer.fidl.h"
#include "lib/media/fidl/media_transport.fidl.h"

namespace media_client {

class AudioOutputStream {
 public:
  AudioOutputStream();
  ~AudioOutputStream();

  bool Initialize(fuchsia_audio_parameters* params,
                  zx_time_t delay,
                  AudioOutputDevice* device);
  int Free() { return device_->FreeStream(this); }

  int GetMinDelay(zx_duration_t* delay_nsec_out);
  int Write(float* sample_buffer, int num_samples, zx_time_t pres_time);

  bool Start();
  void Stop();

  bool active() const { return active_; }

 private:
  bool AcquireRenderer();
  bool SetMediaType(int num_channels, int sample_rate);
  bool CreateMemoryMapping();

  void FillBuffer(float* sample_buffer, int num_samples);
  media::MediaPacketPtr CreateMediaPacket(zx_time_t pts,
                                          size_t payload_offset,
                                          size_t payload_size);
  bool SendMediaPacket(media::MediaPacketPtr packet);

  media::MediaRendererSyncPtr media_renderer_;
  media::MediaPacketConsumerSyncPtr packet_consumer_;
  media::MediaTimelineControlPointSyncPtr timeline_control_point_;

  zx::vmo vmo_;
  size_t bytes_per_frame_ = 0u;
  size_t total_mapping_size_ = 0u;
  int16_t* buffer_ = nullptr;
  size_t current_sample_offset_ = 0u;

  AudioOutputDevice* device_ = nullptr;
  size_t num_channels_ = 0u;
  size_t sample_rate_ = 0u;
  zx_time_t min_delay_nsec_ = 0u;
  bool received_first_frame_ = false;
  zx_time_t start_time_ = 0u;
  bool active_ = false;
};

}  // namespace media_client
