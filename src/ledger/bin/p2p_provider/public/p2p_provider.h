// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_P2P_PROVIDER_PUBLIC_P2P_PROVIDER_H_
#define SRC_LEDGER_BIN_P2P_PROVIDER_PUBLIC_P2P_PROVIDER_H_

#include "src/ledger/bin/p2p_provider/public/types.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/fxl/macros.h"

namespace p2p_provider {
// P2PProvider handles the peer-to-peer connections between devices.
class P2PProvider {
 public:
  class Client {
   public:
    // OnDeviceChange is called for every new connection and
    // disconnection to devices from the mesh, including the ones already
    // participating in the mesh when we connect to it.
    virtual void OnDeviceChange(const P2PClientId& device_name, DeviceChangeType change_type) = 0;
    // OnNewMessage is called for every message sent to this device.
    virtual void OnNewMessage(const P2PClientId& device_name,
                              convert::ExtendedStringView message) = 0;
  };

  P2PProvider() = default;
  virtual ~P2PProvider() = default;

  // Starts participating in the device mesh.
  // To stop participating, destroy this class instance.
  virtual void Start(Client* client) = 0;
  // Sends the provided message |data| to |destination|. Returns true if the
  // message was sent, false if the destination is not available.
  virtual bool SendMessage(const P2PClientId& destination, convert::ExtendedStringView data) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(P2PProvider);
};

}  // namespace p2p_provider

#endif  // SRC_LEDGER_BIN_P2P_PROVIDER_PUBLIC_P2P_PROVIDER_H_
