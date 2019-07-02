// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_provider/testing/make_client_id.h"

#include <fuchsia/overnet/protocol/cpp/fidl.h>

#include "src/ledger/bin/p2p_provider/impl/make_client_id.h"

namespace p2p_provider {

p2p_provider::P2PClientId MakeP2PClientId(uint64_t id) {
  fuchsia::overnet::protocol::NodeId node_id;
  node_id.id = id;
  return MakeP2PClientId(std::move(node_id));
}

}  // namespace p2p_provider
