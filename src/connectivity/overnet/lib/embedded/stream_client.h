// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/connectivity/overnet/lib/embedded/basic_overnet_embedded.h"
#include "src/connectivity/overnet/lib/protocol/stream_framer.h"
#include "src/connectivity/overnet/lib/vocabulary/ip_addr.h"

namespace overnet {

class StreamClientBase : public BasicOvernetEmbedded::Actor {
 public:
  StreamClientBase(BasicOvernetEmbedded* app, IpAddr target);

  const char* Name() const override final { return "StreamClient"; }
  Status Start() override final;

 private:
  const IpAddr target_;

  virtual std::unique_ptr<StreamFramer> CreateFramer() const = 0;
};

template <class T>
class StreamClient final : public StreamClientBase {
 public:
  StreamClient(BasicOvernetEmbedded* app, IpAddr target)
      : StreamClientBase(app, target) {}

 private:
  std::unique_ptr<StreamFramer> CreateFramer() const override {
    return std::make_unique<T>();
  }
};

}  // namespace overnet
