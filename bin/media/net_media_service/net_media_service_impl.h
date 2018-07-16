// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_NET_MEDIA_SERVICE_NET_MEDIA_SERVICE_IMPL_H_
#define GARNET_BIN_MEDIA_NET_MEDIA_SERVICE_NET_MEDIA_SERVICE_IMPL_H_

#include <fuchsia/mediaplayer/cpp/fidl.h>

#include "garnet/bin/media/net_media_service/factory_service_base.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"

namespace media_player {

class NetMediaServiceImpl : public FactoryServiceBase<NetMediaServiceImpl>,
                            public fuchsia::mediaplayer::NetMediaService {
 public:
  NetMediaServiceImpl(
      std::unique_ptr<component::StartupContext> startup_context);
  ~NetMediaServiceImpl() override;

  // NetMediaService implementation.
  void PublishMediaPlayer(
      fidl::StringPtr service_name,
      fidl::InterfaceHandle<fuchsia::mediaplayer::MediaPlayer> media_player)
      override;

  void CreateMediaPlayerProxy(
      fidl::StringPtr device_name, fidl::StringPtr service_name,
      fidl::InterfaceRequest<fuchsia::mediaplayer::MediaPlayer>
          media_player_request) override;

 private:
  fidl::BindingSet<fuchsia::mediaplayer::NetMediaService> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(NetMediaServiceImpl);
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_NET_MEDIA_SERVICE_NET_MEDIA_SERVICE_IMPL_H_
