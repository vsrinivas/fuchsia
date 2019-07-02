// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_P2P_PROVIDER_IMPL_MAKE_CLIENT_ID_H_
#define SRC_LEDGER_BIN_P2P_PROVIDER_IMPL_MAKE_CLIENT_ID_H_

#include <fuchsia/overnet/protocol/cpp/fidl.h>

#include "src/ledger/bin/p2p_provider/public/types.h"

namespace p2p_provider {
// Makes a P2PClientId from a NodeId;
p2p_provider::P2PClientId MakeP2PClientId(fuchsia::overnet::protocol::NodeId node_id);

}  // namespace p2p_provider

#endif  // SRC_LEDGER_BIN_P2P_PROVIDER_IMPL_MAKE_CLIENT_ID_H_
