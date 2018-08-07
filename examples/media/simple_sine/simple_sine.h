// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_MEDIA_SIMPLE_SINE_SIMPLE_SINE_H_
#define GARNET_EXAMPLES_MEDIA_SIMPLE_SINE_SIMPLE_SINE_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/vmo-utils/vmo_mapper.h>

#include "lib/component/cpp/startup_context.h"

namespace examples {

class MediaApp {
 public:
  MediaApp(fit::closure quit_callback);

  void Run(component::StartupContext* app_context);

 private:
  void AcquireRenderer(component::StartupContext* app_context);
  void SetStreamType();

  zx_status_t CreateMemoryMapping();
  void WriteAudioIntoBuffer();

  fuchsia::media::StreamPacket CreatePacket(size_t packet_num);
  void SendPacket(fuchsia::media::StreamPacket packet);
  void OnSendPacketComplete();

  void Shutdown();

  fit::closure quit_callback_;

  fuchsia::media::AudioOutPtr audio_renderer_;

  vmo_utils::VmoMapper payload_buffer_;
  size_t payload_size_;
  size_t total_mapping_size_;
  size_t num_packets_sent_ = 0u;
  size_t num_packets_completed_ = 0u;
};

}  // namespace examples

#endif  // GARNET_EXAMPLES_MEDIA_SIMPLE_SINE_SIMPLE_SINE_H_
