// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/net_media_service/net_media_service_impl.h"

#include "garnet/bin/media/net_media_service/media_player_net_proxy.h"
#include "garnet/bin/media/net_media_service/media_player_net_publisher.h"

namespace media_player {

NetMediaServiceImpl::NetMediaServiceImpl(
    std::unique_ptr<component::ApplicationContext> application_context)
    : FactoryServiceBase(std::move(application_context)) {
  this->application_context()->outgoing_services()->AddService<NetMediaService>(
      [this](fidl::InterfaceRequest<NetMediaService> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

NetMediaServiceImpl::~NetMediaServiceImpl() {}

void NetMediaServiceImpl::PublishMediaPlayer(
    fidl::StringPtr service_name,
    fidl::InterfaceHandle<MediaPlayer> media_player) {
  AddProduct(MediaPlayerNetPublisher::Create(service_name,
                                             std::move(media_player), this));
}

void NetMediaServiceImpl::CreateMediaPlayerProxy(
    fidl::StringPtr device_name,
    fidl::StringPtr service_name,
    fidl::InterfaceRequest<MediaPlayer> media_player_request) {
  AddProduct(MediaPlayerNetProxy::Create(
      device_name, service_name, std::move(media_player_request), this));
}

}  // namespace media_player
