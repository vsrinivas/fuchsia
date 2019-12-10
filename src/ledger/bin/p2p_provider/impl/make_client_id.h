// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_P2P_PROVIDER_IMPL_MAKE_CLIENT_ID_H_
#define SRC_LEDGER_BIN_P2P_PROVIDER_IMPL_MAKE_CLIENT_ID_H_

#include "peridot/lib/rng/random.h"
#include "src/ledger/bin/p2p_provider/public/types.h"

namespace p2p_provider {

p2p_provider::P2PClientId MakeRandomP2PClientId(rng::Random* random);

}  // namespace p2p_provider

#endif  // SRC_LEDGER_BIN_P2P_PROVIDER_IMPL_MAKE_CLIENT_ID_H_
