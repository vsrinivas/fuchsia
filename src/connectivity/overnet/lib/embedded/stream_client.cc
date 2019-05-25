// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/embedded/stream_client.h"

#include "src/connectivity/overnet/lib/embedded/stream_socket_link.h"
#include "src/connectivity/overnet/lib/vocabulary/socket.h"

namespace overnet {

StreamClientBase::StreamClientBase(BasicOvernetEmbedded* app, IpAddr target)
    : BasicOvernetEmbedded::Actor(app), target_(target) {}

Status StreamClientBase::Start() {
  Socket socket;
  return socket.Create(target_.addr.sa_family, SOCK_STREAM, 0)
      .Then([&] { return socket.Connect(target_); })
      .Then([&] {
        RegisterStreamSocketLink(
            root(), std::move(socket), CreateFramer(), true,
            TimeDelta::PositiveInf(), [app = root()] {
              app->Exit(Status(StatusCode::UNAVAILABLE,
                               "Stream server disconnected"));
            });
        return Status::Ok();
      });
}

}  // namespace overnet
