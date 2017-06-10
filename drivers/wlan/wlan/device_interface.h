// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/wlan.h>
#include <magenta/types.h>
#include <mxtl/unique_ptr.h>

namespace wlan {

class Packet;
class Timer;

class DeviceInterface {
  public:
    virtual ~DeviceInterface() {}

    virtual mx_status_t GetTimer(uint64_t id, mxtl::unique_ptr<Timer>* timer) = 0;

    virtual mx_status_t SendEthernet(mxtl::unique_ptr<Packet> packet) = 0;
    virtual mx_status_t SendWlan(mxtl::unique_ptr<Packet> packet) = 0;
    virtual mx_status_t SendService(mxtl::unique_ptr<Packet> packet) = 0;

    virtual mx_status_t SetChannel(wlan_channel_t chan) = 0;
    virtual mx_status_t GetCurrentChannel(wlan_channel_t* chan) = 0;
};

}  // namespace wlan
