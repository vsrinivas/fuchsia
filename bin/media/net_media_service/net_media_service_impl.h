// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_NET_MEDIA_SERVICE_NET_MEDIA_SERVICE_IMPL_H_
#define GARNET_BIN_MEDIA_NET_MEDIA_SERVICE_NET_MEDIA_SERVICE_IMPL_H_

#include <media_player/cpp/fidl.h>
#include "garnet/bin/media/net_media_service/factory_service_base.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"

namespace media_player {

class NetMediaServiceImpl : public FactoryServiceBase<NetMediaServiceImpl>,
                            public NetMediaService {
 public:
  NetMediaServiceImpl(
      std::unique_ptr<component::ApplicationContext> application_context);
  ~NetMediaServiceImpl() override;

  // NetMediaService implementation.
  void PublishMediaPlayer(
      fidl::StringPtr service_name,
      fidl::InterfaceHandle<MediaPlayer> media_player) override;

  void CreateMediaPlayerProxy(
      fidl::StringPtr device_name,
      fidl::StringPtr service_name,
      fidl::InterfaceRequest<MediaPlayer> media_player_request) override;

 private:
  fidl::BindingSet<NetMediaService> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(NetMediaServiceImpl);
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_NET_MEDIA_SERVICE_NET_MEDIA_SERVICE_IMPL_H_
