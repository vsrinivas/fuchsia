// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_provider/impl/make_client_id.h"

#include "peridot/lib/rng/random.h"

namespace p2p_provider {

p2p_provider::P2PClientId MakeRandomP2PClientId(rng::Random* random) {
  std::string random_string = random->RandomUniqueBytes();
  std::vector<uint8_t> random_vector(random_string.begin(), random_string.end());
  return p2p_provider::P2PClientId(std::move(random_vector));
}

}  // namespace p2p_provider
