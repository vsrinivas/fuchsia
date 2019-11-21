// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_P2P_PROVIDER_IMPL_P2P_PROVIDER_IMPL_H_
#define SRC_LEDGER_BIN_P2P_PROVIDER_IMPL_P2P_PROVIDER_IMPL_H_

#include <fuchsia/overnet/cpp/fidl.h>

#include <memory>
#include <set>
#include <string>

#include "lib/fidl/cpp/binding.h"
#include "peridot/lib/rng/random.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/p2p_provider/impl/make_client_id.h"
#include "src/ledger/bin/p2p_provider/impl/remote_connection.h"
#include "src/ledger/bin/p2p_provider/public/p2p_provider.h"
#include "src/ledger/bin/p2p_provider/public/user_id_provider.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/callback/auto_cleanable.h"
#include "src/lib/component/cpp/service_provider_impl.h"

namespace p2p_provider {

struct OvernetNodeIdComparator {
  bool operator()(const fuchsia::overnet::protocol::NodeId& left,
                  const fuchsia::overnet::protocol::NodeId& right) const {
    return (left.id > right.id);
  }
};

// P2PProviderImpl provides the peer-to-peer communication abstraction for the
// Ledger, using Overnet.
//
// We deploy a number of strategies to provide a consistent communication layer
// using Overnet:
// - Overnet exposes services to other devices, and allows one to connect
// to other devices services. We thus expose a service whose name is
// ledger-specific, and also depends on the user id and the ledger version.
// This ensures that only compatible Ledgers of the same user communicate.
// - Overnet provides a list of peers, each with a unique overnet ID and the list
// of services they expose. A given device only initiates connections to peers
// that have the relevant service, and whose overnet ID is greater than their own.
// This ensures that there is only one communication channel between devices.

class P2PProviderImpl : public P2PProvider, public fuchsia::overnet::ServiceProvider {
 public:
  P2PProviderImpl(fuchsia::overnet::OvernetPtr overnet,
                  std::unique_ptr<p2p_provider::UserIdProvider> user_id_provider,
                  rng::Random* random);
  ~P2PProviderImpl() override;

  // P2PProvider:
  void Start(Client* client) override;
  bool SendMessage(const P2PClientId& destination, convert::ExtendedStringView data) override;

  // Overnet.ServiceProvider
  void ConnectToService(zx::channel chan,
                        fuchsia::overnet::ConnectionInfo connection_info) override;

 private:
  // Starts the listening mDNS service.
  void StartService();
  // Retrieves and processes the current devices list
  void ListenForNewDevices();
  // Creates a RemoteConnection from |chan| and associated with |id|.
  void AddConnectionFromChannel(zx::channel chan,
                                std::optional<fuchsia::overnet::protocol::NodeId> overnet_id);
  // Dispatches an incoming message to the relevant page, or sends an error
  // back.
  void Dispatch(P2PClientId source, std::vector<uint8_t> data);
  // Callback when we establish a new device connection, or a device breaks its
  // connection.
  void OnDeviceChange(P2PClientId remote_device, DeviceChangeType change_type);
  // The name of the service exposed by Overnet.
  std::string OvernetServiceName();

  Client* client_ = nullptr;

  // ID of the user, used to ensure all connected Ledgers are for the same user.
  std::string user_id_;

  // Holds the connections and the IDs used to identify them.
  std::map<P2PClientId, RemoteConnection> connections_;

  // The peers that we have initiated a connection to.
  std::set<fuchsia::overnet::protocol::NodeId, OvernetNodeIdComparator> contacted_peers_;

  std::optional<fuchsia::overnet::protocol::NodeId> self_client_id_;

  fidl::Binding<fuchsia::overnet::ServiceProvider> service_binding_;
  fuchsia::overnet::OvernetPtr const overnet_;
  std::unique_ptr<p2p_provider::UserIdProvider> const user_id_provider_;
  rng::Random* random_;

  FXL_DISALLOW_COPY_AND_ASSIGN(P2PProviderImpl);
};

}  // namespace p2p_provider

#endif  // SRC_LEDGER_BIN_P2P_PROVIDER_IMPL_P2P_PROVIDER_IMPL_H_
