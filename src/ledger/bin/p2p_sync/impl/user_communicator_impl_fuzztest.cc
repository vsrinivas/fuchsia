// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>

#include <algorithm>
#include <string>

#include "src/ledger/bin/p2p_provider/impl/p2p_provider_impl.h"
#include "src/ledger/bin/p2p_provider/public/p2p_provider.h"
#include "src/ledger/bin/p2p_sync/impl/user_communicator_impl.h"
#include "src/ledger/bin/storage/public/page_sync_client.h"
#include "src/ledger/bin/storage/testing/page_storage_empty_impl.h"
#include "src/ledger/lib/coroutine/coroutine_impl.h"
#include "src/lib/fxl/macros.h"

namespace p2p_sync {
namespace {

class TestPageStorage : public storage::PageStorageEmptyImpl {
 public:
  TestPageStorage() = default;
  ~TestPageStorage() {}

  storage::PageId GetId() override { return "page"; }

  void SetSyncDelegate(storage::PageSyncDelegate* page_sync) override {
    return;
  }
};

class FuzzingP2PProvider : public p2p_provider::P2PProvider {
 public:
  FuzzingP2PProvider() = default;

  void Start(Client* client) override { client_ = client; }

  bool SendMessage(fxl::StringView destination, fxl::StringView data) override {
    FXL_NOTIMPLEMENTED();
    return false;
  }

  Client* client_;
};

// Fuzz the peer-to-peer messages received by a |UserCommunicatorImpl|.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
  std::string bytes(reinterpret_cast<const char*>(Data), Size);

  coroutine::CoroutineServiceImpl coroutine_service;
  auto provider = std::make_unique<FuzzingP2PProvider>();
  FuzzingP2PProvider* provider_ptr = provider.get();

  UserCommunicatorImpl user_communicator(std::move(provider),
                                         &coroutine_service);
  user_communicator.Start();
  auto ledger_communicator = user_communicator.GetLedgerCommunicator("ledger");

  storage::PageStorageEmptyImpl page_storage;

  auto page_communicator =
      ledger_communicator->GetPageCommunicator(&page_storage, &page_storage);

  provider_ptr->client_->OnDeviceChange("device",
                                        p2p_provider::DeviceChangeType::NEW);
  provider_ptr->client_->OnNewMessage("device", bytes);

  return 0;
}

}  // namespace
}  // namespace p2p_sync
