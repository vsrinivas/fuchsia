// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_P2P_PROVIDER_FAKE_FAKE_P2P_PROVIDER_FACTORY_H_
#define SRC_LEDGER_BIN_P2P_PROVIDER_FAKE_FAKE_P2P_PROVIDER_FACTORY_H_

#include <lib/async/dispatcher.h>

#include <map>

#include "peridot/lib/rng/random.h"
#include "src/ledger/bin/p2p_provider/public/p2p_provider.h"
#include "src/ledger/bin/p2p_provider/public/p2p_provider_factory.h"
#include "src/ledger/bin/p2p_provider/public/types.h"
#include "src/ledger/lib/memory/weak_ptr.h"
#include "src/lib/callback/scoped_task_runner.h"

namespace p2p_provider {
// P2PProvider handles the peer-to-peer connections between devices.
class FakeP2PProviderFactory : public P2PProviderFactory {
 public:
  FakeP2PProviderFactory(rng::Random* random, async_dispatcher_t* dispatcher);
  FakeP2PProviderFactory(const FakeP2PProviderFactory&) = delete;
  FakeP2PProviderFactory& operator=(const FakeP2PProviderFactory&) = delete;
  ~FakeP2PProviderFactory() override;

  std::unique_ptr<P2PProvider> NewP2PProvider(
      async_dispatcher_t* dispatcher, std::unique_ptr<UserIdProvider> user_id_provider) override;

 private:
  class FakeP2PProvider;

  bool SendMessage(P2PClientId source, P2PClientId destination, std::string data);
  void Register(P2PClientId id, ledger::WeakPtr<FakeP2PProvider> provider);
  void Unregister(P2PClientId id);

  std::map<P2PClientId, ledger::WeakPtr<FakeP2PProvider>> providers_;

  rng::Random* const random_;
  callback::ScopedTaskRunner task_runner_;
};

}  // namespace p2p_provider

#endif  // SRC_LEDGER_BIN_P2P_PROVIDER_FAKE_FAKE_P2P_PROVIDER_FACTORY_H_
