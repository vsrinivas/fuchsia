// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_P2P_SYNC_IMPL_USER_COMMUNICATOR_IMPL_H_
#define SRC_LEDGER_BIN_P2P_SYNC_IMPL_USER_COMMUNICATOR_IMPL_H_

#include <memory>
#include <set>
#include <string>

#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/p2p_provider/public/p2p_provider.h"
#include "src/ledger/bin/p2p_provider/public/types.h"
#include "src/ledger/bin/p2p_provider/public/user_id_provider.h"
#include "src/ledger/bin/p2p_sync/impl/device_mesh.h"
#include "src/ledger/bin/p2p_sync/public/user_communicator.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/coroutine/coroutine.h"

namespace p2p_sync {
class LedgerCommunicatorImpl;

// TODO(LE-768): Document the contract of this class in relationship with
// p2p_provider::P2PProvider
class UserCommunicatorImpl : public UserCommunicator,
                             public DeviceMesh,
                             public p2p_provider::P2PProvider::Client {
 public:
  explicit UserCommunicatorImpl(ledger::Environment* environment,
                                std::unique_ptr<p2p_provider::P2PProvider> provider);
  ~UserCommunicatorImpl() override;

  // UserCommunicator:
  void Start() override;
  std::unique_ptr<LedgerCommunicator> GetLedgerCommunicator(std::string namespace_id) override;

  // DeviceMesh:
  DeviceSet GetDeviceList() override;
  void Send(const p2p_provider::P2PClientId& device_name,
            convert::ExtendedStringView data) override;

 private:
  // P2PProvider::Client
  void OnDeviceChange(const p2p_provider::P2PClientId& remote_device,
                      p2p_provider::DeviceChangeType change_type) override;
  void OnNewMessage(const p2p_provider::P2PClientId& source,
                    convert::ExtendedStringView data) override;

  // Set of active ledgers.
  std::map<std::string, LedgerCommunicatorImpl*, convert::StringViewComparator> ledgers_;
  std::set<p2p_provider::P2PClientId> devices_;

  bool started_ = false;

  ledger::Environment* const environment_;
  std::unique_ptr<p2p_provider::P2PProvider> p2p_provider_;
};

}  // namespace p2p_sync

#endif  // SRC_LEDGER_BIN_P2P_SYNC_IMPL_USER_COMMUNICATOR_IMPL_H_
