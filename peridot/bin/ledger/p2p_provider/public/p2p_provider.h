// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_P2P_PROVIDER_PUBLIC_P2P_PROVIDER_H_
#define PERIDOT_BIN_LEDGER_P2P_PROVIDER_PUBLIC_P2P_PROVIDER_H_

#include <lib/fxl/macros.h>
#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/ledger/p2p_provider/public/types.h"

namespace p2p_provider {
// P2PProvider handles the peer-to-peer connections between devices.
class P2PProvider {
 public:
  class Client {
   public:
    // OnDeviceChange is called for every new connection and
    // disconnection to devices from the mesh, including the ones already
    // participating in the mesh when we connect to it.
    virtual void OnDeviceChange(fxl::StringView device_name,
                                DeviceChangeType change_type) = 0;
    // OnNewMessage is called for every message sent to this device.
    virtual void OnNewMessage(fxl::StringView device_name,
                              fxl::StringView message) = 0;
  };

  P2PProvider() {}
  virtual ~P2PProvider() {}

  // Starts participating in the device mesh.
  // To stop participating, destroy this class instance.
  virtual void Start(Client* client) = 0;
  // Sends the provided message |data| to |destination|. Returns true if the
  // message was sent, false if the destination is not available.
  virtual bool SendMessage(fxl::StringView destination,
                           fxl::StringView data) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(P2PProvider);
};

}  // namespace p2p_provider

#endif  // PERIDOT_BIN_LEDGER_P2P_PROVIDER_PUBLIC_P2P_PROVIDER_H_
