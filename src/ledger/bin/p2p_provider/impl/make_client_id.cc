// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_provider/impl/make_client_id.h"

#include <fuchsia/overnet/protocol/c/fidl.h>

namespace p2p_provider {

p2p_provider::P2PClientId MakeP2PClientId(fuchsia::overnet::protocol::NodeId node_id) {
  fidl::Encoder encoder(fidl::Encoder::NO_HEADER);
  // We need to preallocate the size of the structure in the encoder, the rest
  // is allocated when the vector is encoded.
  encoder.Alloc(sizeof(fuchsia_overnet_protocol_NodeId));
  fidl::Encode(&encoder, &node_id, 0);
  return p2p_provider::P2PClientId(encoder.TakeBytes());
}

}  // namespace p2p_provider
