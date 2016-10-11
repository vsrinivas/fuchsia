// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/interfaces/audio_server.mojom.h"
#include "apps/media/src/audio_server/audio_server_impl.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/bindings/binding_set.h"

namespace mojo {
namespace media {
namespace audio {

class AudioServerApp : public ApplicationImplBase {
 public:
  AudioServerApp();
  ~AudioServerApp() override;

  // ApplicationImplBase overrides:
  void OnInitialize() override;
  bool OnAcceptConnection(ServiceProviderImpl* service_provider_impl) override;

 private:
  AudioServerImpl server_impl_;
  BindingSet<AudioServer> bindings_;
};

}  // namespace audio
}  // namespace media
}  // namespace mojo
