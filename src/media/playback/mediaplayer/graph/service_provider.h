// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_SERVICE_PROVIDER_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_SERVICE_PROVIDER_H_

#include <string>

#include "lib/fidl/cpp/interface_ptr.h"
#include "lib/fidl/cpp/interface_request.h"

namespace media_player {

class ServiceProvider {
 public:
  virtual void ConnectToService(std::string service_path, zx::channel channel) = 0;

  template <typename Interface>
  inline fidl::InterfacePtr<Interface> ConnectToService(
      const std::string& service_path = Interface::Name_) {
    fidl::InterfacePtr<Interface> client;
    auto request = client.NewRequest();
    ConnectToService(service_path, request.TakeChannel());
    return client;
  }
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_SERVICE_PROVIDER_H_
