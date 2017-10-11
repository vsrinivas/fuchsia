// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/app/cpp/application_context.h"
#include "lib/media/fidl/media_renderer.fidl.h"
#include "lib/media/fidl/media_transport.fidl.h"

namespace examples {

class MediaApp {
 public:
  MediaApp();
  ~MediaApp();

  void Run(app::ApplicationContext* app_context);

 private:
  void AcquireRenderer(app::ApplicationContext* app_context);
  void SetMediaType();

  zx_status_t CreateMemoryMapping();
  void WriteStereoAudioIntoBuffer();

  media::MediaPacketPtr CreateMediaPacket(size_t packet_num);
  void SendMediaPacket(media::MediaPacketPtr packet);

  void StartPlayback();
  void Shutdown();

  media::MediaRendererPtr media_renderer_;
  media::MediaPacketConsumerPtr packet_consumer_;

  zx::vmo vmo_;
  uintptr_t mapped_address_ = 0u;
  size_t num_packets_completed_ = 0u;
};

}  // namespace examples
