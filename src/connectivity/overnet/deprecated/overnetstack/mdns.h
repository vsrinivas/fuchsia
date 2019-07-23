// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/ref_ptr.h>
#include "src/connectivity/overnet/deprecated/lib/endpoint/router_endpoint.h"
#include "src/connectivity/overnet/deprecated/overnetstack/overnet_app.h"
#include "src/connectivity/overnet/deprecated/overnetstack/udp_nub.h"

namespace overnetstack {

class MdnsIntroducer : public OvernetApp::Actor {
 public:
  MdnsIntroducer(OvernetApp* app, UdpNub* udp_nub);
  ~MdnsIntroducer();
  overnet::Status Start() override;

 private:
  class Impl;
  OvernetApp* const app_;
  UdpNub* const udp_nub_;
  fbl::RefPtr<Impl> impl_;
};

class MdnsAdvertisement : public OvernetApp::Actor {
 public:
  MdnsAdvertisement(OvernetApp* app, UdpNub* udp_nub);
  ~MdnsAdvertisement();
  overnet::Status Start() override;

 private:
  OvernetApp* const app_;
  UdpNub* const udp_nub_;
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace overnetstack
