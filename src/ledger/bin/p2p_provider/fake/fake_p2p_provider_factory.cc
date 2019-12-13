// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_provider/fake/fake_p2p_provider_factory.h"

#include <memory>

#include "lib/async/cpp/task.h"
#include "lib/fit/function.h"
#include "src/ledger/bin/p2p_provider/impl/make_client_id.h"
#include "src/ledger/bin/p2p_provider/public/types.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/memory/weak_ptr.h"
#include "src/lib/callback/scoped_callback.h"
#include "src/lib/callback/scoped_task_runner.h"

namespace p2p_provider {
class FakeP2PProviderFactory::FakeP2PProvider : public P2PProvider {
 public:
  FakeP2PProvider(P2PClientId id, async_dispatcher_t *dispatcher, FakeP2PProviderFactory *factory)
      : id_(std::move(id)), dispatcher_(dispatcher), factory_(factory), weak_factory_(this) {
    factory_->Register(id_, weak_factory_.GetWeakPtr());
  }

  ~FakeP2PProvider() override { factory_->Unregister(id_); }

  void Start(Client *client) override { client_ = client; }

  bool SendMessage(const P2PClientId &destination, convert::ExtendedStringView data) override {
    return factory_->SendMessage(id_, destination, convert::ToString(data));
  }

  void ReceiveMessage(P2PClientId source, std::string data) {
    async::PostTask(dispatcher_, callback::MakeScoped(
                                     weak_factory_.GetWeakPtr(),
                                     [this, source = std::move(source), data = std::move(data)] {
                                       client_->OnNewMessage(source, data);
                                     }));
  }

  void OnDeviceChange(P2PClientId source, DeviceChangeType change_type) {
    async::PostTask(
        dispatcher_,
        callback::MakeScoped(weak_factory_.GetWeakPtr(), [this, source = std::move(source),
                                                          change_type = std::move(change_type)] {
          client_->OnDeviceChange(source, change_type);
        }));
  }

 private:
  P2PClientId const id_;
  async_dispatcher_t *const dispatcher_;
  FakeP2PProviderFactory *const factory_;

  Client *client_;

  ledger::WeakPtrFactory<FakeP2PProviderFactory::FakeP2PProvider> weak_factory_;
};

FakeP2PProviderFactory::FakeP2PProviderFactory(rng::Random *random, async_dispatcher_t *dispatcher)
    : random_(random), task_runner_(dispatcher) {}

FakeP2PProviderFactory::~FakeP2PProviderFactory() {
  for (auto [id, provider] : providers_) {
    // All providers should be deleted.
    LEDGER_CHECK(!provider);
  }
}

std::unique_ptr<P2PProvider> FakeP2PProviderFactory::NewP2PProvider(
    async_dispatcher_t *dispatcher, std::unique_ptr<UserIdProvider> /*user_id_provider*/) {
  auto id = MakeRandomP2PClientId(random_);
  auto provider = std::make_unique<FakeP2PProvider>(id, dispatcher, this);
  return provider;
}

bool FakeP2PProviderFactory::SendMessage(P2PClientId source, P2PClientId destination,
                                         std::string data) {
  auto it = providers_.find(destination);
  if (it == providers_.end()) {
    return false;
  }
  // Move to the factory run loop to simulate the network.
  task_runner_.PostTask([this, source = std::move(source), destination = std::move(destination),
                         data = std::move(data)] {
    auto it = providers_.find(destination);
    if (it != providers_.end() && it->second) {
      it->second->ReceiveMessage(source, data);
    }
  });
  return true;
}

void FakeP2PProviderFactory::Register(P2PClientId id, ledger::WeakPtr<FakeP2PProvider> provider) {
  // Move to the factory run loop to simulate the network.
  task_runner_.PostTask([this, new_id = std::move(id), new_provider = std::move(provider)] {
    providers_.emplace(new_id, new_provider);
    for (const auto &[id, provider] : providers_) {
      if (id == new_id) {
        continue;
      }
      if (provider) {
        provider->OnDeviceChange(new_id, DeviceChangeType::NEW);
      }
      if (new_provider) {
        new_provider->OnDeviceChange(id, DeviceChangeType::NEW);
      }
    }
  });
}

void FakeP2PProviderFactory::Unregister(P2PClientId id) {
  // Move to the factory run loop to simulate the network.
  task_runner_.PostTask([this, old_id = std::move(id)] {
    LEDGER_CHECK(providers_.erase(old_id) == 1);
    for (const auto &[id, provider] : providers_) {
      if (id == old_id) {
        continue;
      }
      if (provider) {
        provider->OnDeviceChange(old_id, DeviceChangeType::DELETED);
      }
    }
  });
}

}  // namespace p2p_provider
