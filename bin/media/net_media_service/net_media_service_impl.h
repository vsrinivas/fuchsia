// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/util/factory_service_base.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/media/fidl/net_media_service.fidl.h"

namespace media {

class NetMediaServiceImpl : public FactoryServiceBase<NetMediaServiceImpl>,
                            public NetMediaService {
 public:
  NetMediaServiceImpl(
      std::unique_ptr<component::ApplicationContext> application_context);
  ~NetMediaServiceImpl() override;

  // NetMediaService implementation.
  void CreateNetMediaPlayer(
      const f1dl::String& service_name,
      f1dl::InterfaceHandle<MediaPlayer> media_player,
      f1dl::InterfaceRequest<NetMediaPlayer> net_media_player_request) override;

  void CreateNetMediaPlayerProxy(
      const f1dl::String& device_name,
      const f1dl::String& service_name,
      f1dl::InterfaceRequest<NetMediaPlayer> net_media_player_request) override;

 private:
  f1dl::BindingSet<NetMediaService> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(NetMediaServiceImpl);
};

}  // namespace media
