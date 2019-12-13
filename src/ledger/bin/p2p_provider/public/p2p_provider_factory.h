// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_P2P_PROVIDER_PUBLIC_P2P_PROVIDER_FACTORY_H_
#define SRC_LEDGER_BIN_P2P_PROVIDER_PUBLIC_P2P_PROVIDER_FACTORY_H_

#include <lib/async/dispatcher.h>

#include <memory>

#include "src/ledger/bin/p2p_provider/public/p2p_provider.h"
#include "src/ledger/bin/p2p_provider/public/user_id_provider.h"

namespace p2p_provider {
// P2PProvider handles the peer-to-peer connections between devices.
class P2PProviderFactory {
 public:
  P2PProviderFactory() = default;
  virtual ~P2PProviderFactory() = default;

  virtual std::unique_ptr<P2PProvider> NewP2PProvider(
      async_dispatcher_t* dispatcher, std::unique_ptr<UserIdProvider> user_id_provider) = 0;
};

}  // namespace p2p_provider

#endif  // SRC_LEDGER_BIN_P2P_PROVIDER_PUBLIC_P2P_PROVIDER_FACTORY_H_
