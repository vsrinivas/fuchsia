// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <garnet/lib/overnet/router_endpoint.h>
#include "lib/component/cpp/startup_context.h"
#include "udp_nub.h"

namespace overnetstack {

void RunMdnsIntroducer(component::StartupContext* startup_context,
                       UdpNub* udp_nub);

class MdnsAdvertisement {
 public:
  MdnsAdvertisement(component::StartupContext* startup_context,
                    UdpNub* udp_nub);
  ~MdnsAdvertisement();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace overnetstack
