// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/connectivity/overnet/lib/embedded/basic_overnet_embedded.h"
#include "src/connectivity/overnet/lib/protocol/stream_framer.h"
#include "src/connectivity/overnet/lib/vocabulary/ip_addr.h"
#include "src/connectivity/overnet/lib/vocabulary/socket.h"

namespace overnet {

class StreamServerBase : public BasicOvernetEmbedded::Actor {
 public:
  StreamServerBase(BasicOvernetEmbedded* app, IpAddr bind);

  const char* Name() const override { return "StreamServer"; }
  Status Start() override;

 private:
  void AwaitRead();
  virtual std::unique_ptr<StreamFramer> CreateFramer() = 0;

  BasicOvernetEmbedded* const app_;
  const IpAddr bind_;
  Socket listener_;
};

template <class T>
class StreamServer final : public StreamServerBase {
 public:
  StreamServer(BasicOvernetEmbedded* app, IpAddr bind)
      : StreamServerBase(app, bind) {}

 private:
  virtual std::unique_ptr<StreamFramer> CreateFramer() {
    return std::make_unique<T>();
  }
};

}  // namespace overnet
