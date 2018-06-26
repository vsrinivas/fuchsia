// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_USER_COMMUNICATOR_IMPL_H_
#define PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_USER_COMMUNICATOR_IMPL_H_

#include <memory>
#include <set>
#include <string>

#include <fuchsia/netconnector/cpp/fidl.h>

#include "lib/app/cpp/service_provider_impl.h"
#include "lib/callback/auto_cleanable.h"
#include "lib/netconnector/cpp/message_relay.h"
#include "peridot/bin/ledger/p2p_provider/public/p2p_provider.h"
#include "peridot/bin/ledger/p2p_provider/public/types.h"
#include "peridot/bin/ledger/p2p_provider/public/user_id_provider.h"
#include "peridot/bin/ledger/p2p_sync/impl/device_mesh.h"
#include "peridot/bin/ledger/p2p_sync/public/user_communicator.h"
#include "peridot/lib/convert/convert.h"

namespace p2p_sync {
class LedgerCommunicatorImpl;

class UserCommunicatorImpl : public UserCommunicator,
                             public DeviceMesh,
                             public p2p_provider::P2PProvider::Client {
 public:
  explicit UserCommunicatorImpl(
      std::unique_ptr<p2p_provider::P2PProvider> provider);
  ~UserCommunicatorImpl() override;

  // UserCommunicator:
  void Start() override;
  std::unique_ptr<LedgerCommunicator> GetLedgerCommunicator(
      std::string namespace_id) override;

  // DeviceMesh:
  const DeviceSet& GetDeviceList() override;
  void Send(fxl::StringView device_name, fxl::StringView data) override;

 private:
  // P2PProvider::Client
  void OnDeviceChange(fxl::StringView device_name,
                      p2p_provider::DeviceChangeType change_type) override;
  void OnNewMessage(fxl::StringView device_name,
                    fxl::StringView message) override;

  // Set of active ledgers.
  std::map<std::string, LedgerCommunicatorImpl*, convert::StringViewComparator>
      ledgers_;
  std::set<std::string, convert::StringViewComparator> devices_;

  bool started_ = false;
  std::string user_token_;
  std::unique_ptr<fuchsia::sys::ServiceProviderImpl> network_service_provider_;

  std::unique_ptr<p2p_provider::P2PProvider> p2p_provider_;
};

}  // namespace p2p_sync

#endif  // PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_USER_COMMUNICATOR_IMPL_H_
