// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_STA_IFC_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_STA_IFC_H_

#include <wlan/protocol/info.h>

namespace wlan::simulation {

enum EnvironmentEventType {
  kEventDone,    // Simulation has ended -- no more events
  kEventStaReq,  // Station-requested event
};

class StationIfc {
 public:
  // Placeholder for eventual packet-level packet handler
  virtual void Rx(void* pkt) = 0;

  // Simplified beacon handler, eventually to be incorporated into Rx() functionality
  virtual void RxBeacon(wlan_channel_t* channel, wlan_ssid_t* ssid) = 0;

  // Receive notification of a simulation event
  virtual void ReceiveNotification(enum EnvironmentEventType notification_type, void* payload) = 0;
};

}  // namespace wlan::simulation

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_STA_IFC_H_
