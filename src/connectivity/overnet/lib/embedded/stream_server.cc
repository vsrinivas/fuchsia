// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/embedded/stream_server.h"

#include "src/connectivity/overnet/lib/embedded/stream_socket_link.h"

namespace overnet {

StreamServerBase::StreamServerBase(BasicOvernetEmbedded* app, IpAddr bind)
    : Actor(app), app_(app), bind_(bind) {}

Status StreamServerBase::Start() {
  return listener_.Create(bind_.addr.sa_family, SOCK_STREAM, 0)
      .Then([&] { return listener_.Bind(bind_); })
      .Then([&] { return listener_.Listen(); })
      .Then([&] {
        AwaitRead();
        return Status::Ok();
      })
      .WithLazyContext([this] {
        std::ostringstream out;
        out << "Building stream server " << bind_;
        return out.str();
      });
}

void StreamServerBase::AwaitRead() {
  app_->reactor()->OnRead(listener_.get(), [&](const Status& status) {
    if (status.is_error()) {
      return;
    }
    auto fd = listener_.Accept();
    if (fd.is_error()) {
      OVERNET_TRACE(ERROR) << "Failed to accept socket: " << fd.AsStatus();
      return;
    }
    RegisterStreamSocketLink(app_, std::move(*fd), CreateFramer(), true,
                             TimeDelta::PositiveInf(),
                             Callback<void>::Ignored());
    AwaitRead();
  });
}

}  // namespace overnet
