// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

#include "garnet/lib/media/audio_output_device.h"
#include "lib/media/c/audio.h"
#include "lib/media/fidl/audio_renderer.fidl.h"
#include "lib/media/fidl/media_renderer.fidl.h"
#include "lib/media/fidl/media_transport.fidl.h"

namespace media_client {

class AudioOutputStream {
 public:
  AudioOutputStream();
  ~AudioOutputStream();

  bool Initialize(fuchsia_audio_parameters* params, AudioOutputDevice* device);
  int Free() { return device_->FreeStream(this); }

  int GetMinDelay(zx_duration_t* delay_nsec_out);
  int SetGain(float db_gain);
  int Write(float* sample_buffer, int num_samples, zx_time_t pres_time);

  bool Start();
  void Stop();

 private:
  bool AcquireRenderer();
  bool SetMediaType(int num_channels, int sample_rate);
  bool CreateMemoryMapping();
  bool GetDelays();

  void PullFromClientBuffer(float* client_buffer, int num_samples);
  media::MediaPacketPtr CreateMediaPacket(zx_time_t pts,
                                          size_t payload_offset,
                                          size_t payload_size);
  bool SendMediaPacket(media::MediaPacketPtr packet);

  media::AudioRendererSyncPtr audio_renderer_;
  media::MediaRendererSyncPtr media_renderer_;
  media::MediaPacketConsumerSyncPtr packet_consumer_;
  media::MediaTimelineControlPointSyncPtr timeline_control_point_;

  zx::vmo vmo_;
  int total_mapping_samples_ = 0;
  int16_t* buffer_ = nullptr;

  AudioOutputDevice* device_ = nullptr;
  int num_channels_ = 0;
  int sample_rate_ = 0;
  int64_t delay_nsec_ = 0;

  bool active_ = false;
  bool received_first_frame_ = false;
  zx_time_t start_time_ = 0u;
  int current_sample_offset_ = 0;
  float renderer_db_gain_ = 0.0f;
};

}  // namespace media_client
