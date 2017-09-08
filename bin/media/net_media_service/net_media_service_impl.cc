// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/net_media_service/net_media_service_impl.h"

#include "garnet/bin/media/net_media_service/net_media_player_impl.h"
#include "garnet/bin/media/net_media_service/net_media_player_net_proxy.h"

namespace media {

NetMediaServiceImpl::NetMediaServiceImpl(
    std::unique_ptr<app::ApplicationContext> application_context)
    : FactoryServiceBase(std::move(application_context)) {
    this->application_context()->outgoing_services()->AddService<NetMediaService>(
      [this](fidl::InterfaceRequest<NetMediaService> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

NetMediaServiceImpl::~NetMediaServiceImpl() {}

void NetMediaServiceImpl::CreateNetMediaPlayer(
    const fidl::String& service_name,
    fidl::InterfaceHandle<MediaPlayer> media_player,
    fidl::InterfaceRequest<NetMediaPlayer> net_media_player_request) {
  AddProduct(NetMediaPlayerImpl::Create(service_name, std::move(media_player),
                                        std::move(net_media_player_request),
                                        this));
}

void NetMediaServiceImpl::CreateNetMediaPlayerProxy(
    const fidl::String& device_name,
    const fidl::String& service_name,
    fidl::InterfaceRequest<NetMediaPlayer> net_media_player_request) {
  AddProduct(NetMediaPlayerNetProxy::Create(
      device_name, service_name, std::move(net_media_player_request), this));
}

}  // namespace media
