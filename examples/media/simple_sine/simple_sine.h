// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/vmo_mapper.h>
#include <media/cpp/fidl.h>
#include "lib/app/cpp/application_context.h"
#include "lib/fxl/functional/closure.h"

namespace examples {

class MediaApp {
 public:
  MediaApp(fxl::Closure quit_callback);

  void set_float(bool enable_float) { use_float_ = enable_float; }

  void Run(component::ApplicationContext* app_context);

 private:
  void AcquireRenderer(component::ApplicationContext* app_context);
  void SetMediaType();

  zx_status_t CreateMemoryMapping();
  void WriteAudioIntoBuffer();

  media::AudioPacket CreateAudioPacket(size_t packet_num);
  void SendPacket(media::AudioPacket packet);
  void OnSendPacketComplete();

  void Shutdown();

  fxl::Closure quit_callback_;

  media::AudioRenderer2Ptr audio_renderer_;

  fbl::VmoMapper payload_buffer_;
  size_t sample_size_;
  size_t payload_size_;
  size_t total_mapping_size_;
  size_t num_packets_sent_ = 0u;
  size_t num_packets_completed_ = 0u;

  bool use_float_ = false;
};

}  // namespace examples
