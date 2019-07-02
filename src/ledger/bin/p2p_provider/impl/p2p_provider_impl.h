// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_P2P_PROVIDER_IMPL_P2P_PROVIDER_IMPL_H_
#define SRC_LEDGER_BIN_P2P_PROVIDER_IMPL_P2P_PROVIDER_IMPL_H_

#include <fuchsia/overnet/cpp/fidl.h>
#include <lib/callback/auto_cleanable.h>
#include <lib/component/cpp/service_provider_impl.h>

#include <memory>
#include <set>
#include <string>

#include "lib/fidl/cpp/binding.h"
#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/p2p_provider/impl/make_client_id.h"
#include "src/ledger/bin/p2p_provider/impl/remote_connection.h"
#include "src/ledger/bin/p2p_provider/public/p2p_provider.h"
#include "src/ledger/bin/p2p_provider/public/user_id_provider.h"

namespace p2p_provider {
// P2PProviderImpl provides the peer-to-peer communication abstraction for the
// Ledger, using Overnet.
//
// We deploy a number of strategies to provide a consistent communication layer
// using Overnet:
// - Overnet exposes services to other devices, and allows one to connect
// to other devices services. We thus expose a service whose name is
// ledger-specific, and also depends on the user id. This ensures only Ledgers
// of the same user communicate.
// - Overnet does not provide the connected service with the identity of
// the device that connects to it. For this, the connecting device sends an
// handshake as its first message to ascertain its identity.
// - Overnet does not allow enumeration of services on remote devices, and
// there is no built-in connection confirmation: when we connect, we are not
// sure whether the remote device has the service we want. Thus, the connected
// device also sends an handshake to confirm the connection.
class P2PProviderImpl : public P2PProvider, public fuchsia::overnet::ServiceProvider {
 public:
  P2PProviderImpl(fuchsia::overnet::OvernetPtr overnet,
                  std::unique_ptr<p2p_provider::UserIdProvider> user_id_provider);
  ~P2PProviderImpl() override;

  // P2PProvider:
  void Start(Client* client) override;
  bool SendMessage(const P2PClientId& destination, fxl::StringView data) override;

  // Overnet.ServiceProvider
  void ConnectToService(zx::channel chan) override;

 private:
  // Starts the listening mDNS service.
  void StartService();
  // Processes the first message on a new connection. If
  // |should_send_handshake|, a handshake is also sent back on the connection;
  // this happens when the connection was established by the other side.
  void ProcessHandshake(RemoteConnection* connection, std::vector<uint8_t> data,
                        bool should_send_handshake, std::optional<P2PClientId> network_remote_node);
  // Retrieves and processes the current devices list
  void ListenForNewDevices(uint64_t version);
  // Dispatches an incoming message to the relevant page, or sends an error
  // back.
  void Dispatch(P2PClientId source, std::vector<uint8_t> data);
  // Callback when we establish a new device connection, or a device breaks its
  // connection.
  void OnDeviceChange(P2PClientId remote_device, DeviceChangeType change_type);

  Client* client_ = nullptr;

  // ID of a user, used to ensure all connected Ledgers are for the same user.
  std::string user_id_;
  // |connection_map_| holds the connections, keyed by the remote host name.
  std::map<P2PClientId, RemoteConnection*> connection_map_;
  // |connections_| holds the set of all established peer-to-peer connections.
  // We need both |connections_| and |connection_map_| as inbound connections
  // don't have an assiociated host name until we receive the handshake.
  callback::AutoCleanableSet<RemoteConnection> connections_;
  // |contacted_hosts_| lists all the hosts we tried to contact so far that
  // remain visible to us. This is useful as, when we open a connection to a
  // device, we don't know if it holds a ledger for the user we want; this set
  // allows us to not end up in an infinite loop of: new device->connect->no
  // ledger for our user->disconnect->new device!->...
  // Once a device become invisible (disconnected from the local network, shut
  // down, ...), we remove it from this set.
  std::set<P2PClientId> contacted_hosts_;
  // We can receive requests between the time we appear on the network and the
  // time we are able to process them (ie. when we know our own node ID). We
  // just store them here in the meantime.
  std::vector<zx::channel> pending_requests_;

  std::optional<P2PClientId> self_client_id_;

  fidl::Binding<fuchsia::overnet::ServiceProvider> service_binding_;
  fuchsia::overnet::OvernetPtr const overnet_;
  std::unique_ptr<p2p_provider::UserIdProvider> const user_id_provider_;

  FXL_DISALLOW_COPY_AND_ASSIGN(P2PProviderImpl);
};

}  // namespace p2p_provider

#endif  // SRC_LEDGER_BIN_P2P_PROVIDER_IMPL_P2P_PROVIDER_IMPL_H_
