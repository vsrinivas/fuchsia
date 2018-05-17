// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/sm/smp.h"

namespace btlib {
namespace sm {
namespace util {

// Used to select the key generation method as described in Vol 3, Part H,
// 2.3.5.1 based on local and peer authentication parameters:
//   - |secure_connections|: True if Secure Connections pairing is used. False
//     means Legacy Pairing.
//   - |local_oob|: Local OOB auth data is available.
//   - |peer_oob|: Peer OOB auth data is available.
//   - |mitm_required|: True means at least one of the devices requires MITM
//     protection.
//   - |local_ioc|, |peer_ioc|: Indicate local and peer IO capabilities.
PairingMethod SelectPairingMethod(bool secure_connections, bool local_oob,
                                  bool peer_oob, bool mitm_required,
                                  IOCapability local_ioc,
                                  IOCapability peer_ioc);

}  // namespace util
}  // namespace sm
}  // namespace btlib
