// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "low_energy_connection_manager.h"

#include <zircon/assert.h>

#include "garnet/drivers/bluetooth/lib/gatt/local_service_manager.h"
#include "garnet/drivers/bluetooth/lib/hci/defaults.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"
#include "garnet/drivers/bluetooth/lib/hci/util.h"
#include "garnet/drivers/bluetooth/lib/l2cap/channel_manager.h"
#include "garnet/drivers/bluetooth/lib/sm/pairing_state.h"
#include "garnet/drivers/bluetooth/lib/sm/util.h"

#include "lib/fxl/random/rand.h"
#include "lib/fxl/strings/string_printf.h"

#include "pairing_delegate.h"
#include "remote_device.h"
#include "remote_device_cache.h"

namespace btlib {
namespace gap {

using common::DeviceAddress;

namespace internal {

// Represents the state of an active connection. Each instance is owned
// and managed by a LowEnergyConnectionManager and is kept alive as long as
// there is at least one LowEnergyConnectionRef that references it.
class LowEnergyConnection final : public sm::PairingState::Delegate {
 public:
  LowEnergyConnection(const std::string& id,
                      std::unique_ptr<hci::Connection> link,
                      async_dispatcher_t* dispatcher,
                      fxl::WeakPtr<LowEnergyConnectionManager> conn_mgr)
      : id_(id),
        link_(std::move(link)),
        dispatcher_(dispatcher),
        conn_mgr_(conn_mgr),
        weak_ptr_factory_(this) {
    ZX_DEBUG_ASSERT(!id_.empty());
    ZX_DEBUG_ASSERT(link_);
    ZX_DEBUG_ASSERT(dispatcher_);
    ZX_DEBUG_ASSERT(conn_mgr_);
  }

  ~LowEnergyConnection() override {
    // Tell the controller to disconnect the link if it is marked as open.
    link_->Close();

    // Notify all active references that the link is gone. This will
    // synchronously notify all refs.
    CloseRefs();
  }

  LowEnergyConnectionRefPtr AddRef() {
    LowEnergyConnectionRefPtr conn_ref(
        new LowEnergyConnectionRef(id_, handle(), conn_mgr_));
    ZX_ASSERT(conn_ref);

    refs_.insert(conn_ref.get());

    bt_log(TRACE, "gap-le", "added ref (handle %#.4x, count: %lu)", handle(),
           ref_count());

    return conn_ref;
  }

  void DropRef(LowEnergyConnectionRef* ref) {
    ZX_DEBUG_ASSERT(ref);

    __UNUSED size_t res = refs_.erase(ref);
    ZX_DEBUG_ASSERT_MSG(res == 1u,
                        "DropRef called with wrong connection reference");
    bt_log(TRACE, "gap-le", "dropped ref (handle: %#.4x, count: %lu)", handle(),
           ref_count());
  }

  // Registers this connection with L2CAP and initializes the fixed channel
  // protocols.
  void InitializeFixedChannels(fbl::RefPtr<data::Domain> data_domain,
                               fbl::RefPtr<gatt::GATT> gatt,
                               l2cap::LEConnectionParameterUpdateCallback cp_cb,
                               l2cap::LinkErrorCallback link_error_cb) {
    auto self = weak_ptr_factory_.GetWeakPtr();
    auto channels_cb = [self, gatt](fbl::RefPtr<l2cap::Channel> att,
                                    fbl::RefPtr<l2cap::Channel> smp) {
      if (!self || !att || !smp) {
        bt_log(TRACE, "gap-le",
               "link was closed before opening fixed channels");
        return;
      }

      // Obtain existing pairing data, if any.
      std::optional<sm::LTK> ltk;
      auto* dev = self->conn_mgr_->device_cache()->FindDeviceById(self->id_);
      ZX_DEBUG_ASSERT_MSG(dev, "connected device must be present in cache!");

      if (dev->le() && dev->le()->bond_data() && dev->le()->bond_data()->ltk) {
        ltk = *dev->le()->bond_data()->ltk;
      }

      // Obtain the local I/O capabilities from the delegate. Default to
      // NoInputNoOutput if no delegate is available.
      auto io_cap = sm::IOCapability::kNoInputNoOutput;
      if (self->conn_mgr_->pairing_delegate()) {
        io_cap = self->conn_mgr_->pairing_delegate()->io_capability();
      }
      auto pairing = std::make_unique<sm::PairingState>(
          self->link_->WeakPtr(), std::move(smp), io_cap, self);

      // TODO(NET-1151): We register the connection with GATT but don't perform
      // service discovery until after pairing has completed. Fix this so that
      // services are discovered immediately and pairing happens in response to
      // a service request unless the peer is already paired.
      gatt->AddConnection(self->id(), std::move(att));

      if (ltk) {
        bt_log(INFO, "gap-le", "encrypting link with existing LTK");
        pairing->SetCurrentSecurity(*ltk);
        gatt->DiscoverServices(self->id_);
      } else {
        bt_log(INFO, "gap-le", "pairing with device");
        pairing->UpgradeSecurity(
            sm::SecurityLevel::kEncrypted,
            [gatt, id = self->id()](sm::Status status, const auto& props) {
              bt_log(INFO, "gap-le", "pairing status: %s, properties: %s",
                     status.ToString().c_str(), props.ToString().c_str());
              gatt->DiscoverServices(id);
            });
      }
      self->pairing_ = std::move(pairing);
    };

    data_domain->AddLEConnection(
        link_->handle(), link_->role(), std::move(link_error_cb),
        std::move(channels_cb), std::move(cp_cb), dispatcher_);
  }

  // Cancels any on-going pairing procedures and sets up SMP to use the provided
  // new I/O capabilities for future pairing procedures.
  void ResetPairingState(sm::IOCapability ioc) { pairing_->Reset(ioc); }

  size_t ref_count() const { return refs_.size(); }

  const std::string& id() const { return id_; }
  hci::ConnectionHandle handle() const { return link_->handle(); }
  hci::Connection* link() const { return link_.get(); }

 private:
  // sm::PairingState::Delegate override:
  void OnNewPairingData(const sm::PairingData& pairing_data) override {
    // Consider the pairing temporary if no link key was received. This
    // means we'll remain encrypted with the STK without creating a bond and
    // reinitiate pairing when we reconnect in the future.
    // TODO(armansito): Support bonding with just the CSRK for LE security mode
    // 2.
    if (!pairing_data.ltk) {
      bt_log(INFO, "gap-le", "temporarily paired with device (id: %s)",
             id().c_str());
      return;
    }

    bt_log(INFO, "gap-le", "new pairing data [%s%s%s%sid: %s]",
           pairing_data.ltk ? "ltk " : "", pairing_data.irk ? "irk " : "",
           pairing_data.identity_address
               ? fxl::StringPrintf(
                     "(identity: %s) ",
                     pairing_data.identity_address->ToString().c_str())
                     .c_str()
               : "",
           pairing_data.csrk ? "csrk " : "", id().c_str());

    if (!conn_mgr_->device_cache()->StoreLowEnergyBond(id_, pairing_data)) {
      bt_log(ERROR, "gap-le", "failed to cache bonding data (id: %s)",
             id().c_str());
    }
  }

  // sm::PairingState::Delegate override:
  void OnPairingComplete(sm::Status status) override {
    bt_log(TRACE, "gap-le", "pairing complete: %s", status.ToString().c_str());
    auto delegate = conn_mgr_->pairing_delegate();
    if (delegate) {
      delegate->CompletePairing(id_, status);
    }
  }

  // sm::PairingState::Delegate override:
  void OnAuthenticationFailure(hci::Status status) override {
    // TODO(armansito): Clear bonding data from the remote device cache as any
    // stored link key is not valid.
    bt_log(ERROR, "gap-le", "link layer authentication failed: %s",
           status.ToString().c_str());
  }

  // sm::PairingState::Delegate override:
  void OnTemporaryKeyRequest(
      sm::PairingMethod method,
      sm::PairingState::Delegate::TkResponse responder) override {
    bt_log(TRACE, "gap-le", "TK request - method: %s",
           sm::util::PairingMethodToString(method).c_str());

    auto delegate = conn_mgr_->pairing_delegate();
    if (!delegate) {
      bt_log(ERROR, "gap-le", "rejecting pairing without a PairingDelegate!");
      responder(false, 0);
      return;
    }

    if (method == sm::PairingMethod::kPasskeyEntryInput) {
      // The TK will be provided by the user.
      delegate->RequestPasskey(
          id(), [responder = std::move(responder)](int64_t passkey) {
            if (passkey < 0) {
              responder(false, 0);
            } else {
              responder(true, static_cast<uint32_t>(passkey));
            }
          });
      return;
    }

    if (method == sm::PairingMethod::kPasskeyEntryDisplay) {
      // Randomly generate a 6 digit passkey.
      // TODO(armansito): Use a uniform prng.
      uint32_t passkey = fxl::RandUint64() % 1000000;
      delegate->DisplayPasskey(
          id(), passkey,
          [passkey, responder = std::move(responder)](bool confirm) {
            responder(confirm, passkey);
          });
      return;
    }

    // TODO(armansito): Support providing a TK out of band.
    // OnTKRequest() should only be called for legacy pairing.
    ZX_DEBUG_ASSERT(method == sm::PairingMethod::kJustWorks);

    delegate->ConfirmPairing(id(),
                             [responder = std::move(responder)](bool confirm) {
                               // The TK for Just Works pairing is 0 (Vol 3,
                               // Part H, 2.3.5.2).
                               responder(confirm, 0);
                             });
  }

  void CloseRefs() {
    for (auto* ref : refs_) {
      ref->MarkClosed();
    }

    refs_.clear();
  }

  std::string id_;
  std::unique_ptr<hci::Connection> link_;
  async_dispatcher_t* dispatcher_;
  fxl::WeakPtr<LowEnergyConnectionManager> conn_mgr_;

  // SMP pairing manager.
  std::unique_ptr<sm::PairingState> pairing_;

  // LowEnergyConnectionManager is responsible for making sure that these
  // pointers are always valid.
  std::unordered_set<LowEnergyConnectionRef*> refs_;

  fxl::WeakPtrFactory<LowEnergyConnection> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LowEnergyConnection);
};

}  // namespace internal

LowEnergyConnectionRef::LowEnergyConnectionRef(
    const std::string& device_id, hci::ConnectionHandle handle,
    fxl::WeakPtr<LowEnergyConnectionManager> manager)
    : active_(true), device_id_(device_id), handle_(handle), manager_(manager) {
  ZX_DEBUG_ASSERT(!device_id_.empty());
  ZX_DEBUG_ASSERT(manager_);
  ZX_DEBUG_ASSERT(handle_);
}

LowEnergyConnectionRef::~LowEnergyConnectionRef() {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
  if (active_) {
    Release();
  }
};

void LowEnergyConnectionRef::Release() {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
  ZX_DEBUG_ASSERT(active_);
  active_ = false;
  if (manager_) {
    manager_->ReleaseReference(this);
  }
}

void LowEnergyConnectionRef::MarkClosed() {
  active_ = false;
  if (closed_cb_) {
    // Move the callback out of |closed_cb_| to prevent it from deleting itself
    // by deleting |this|.
    auto f = std::move(closed_cb_);
    f();
  }
}

LowEnergyConnectionManager::PendingRequestData::PendingRequestData(
    const DeviceAddress& address, ConnectionResultCallback first_callback)
    : address_(address) {
  callbacks_.push_back(std::move(first_callback));
}

void LowEnergyConnectionManager::PendingRequestData::NotifyCallbacks(
    hci::Status status, const RefFunc& func) {
  ZX_DEBUG_ASSERT(!callbacks_.empty());
  for (const auto& callback : callbacks_) {
    callback(status, func());
  }
}

LowEnergyConnectionManager::LowEnergyConnectionManager(
    fxl::RefPtr<hci::Transport> hci, hci::LowEnergyConnector* connector,
    RemoteDeviceCache* device_cache, fbl::RefPtr<data::Domain> data_domain,
    fbl::RefPtr<gatt::GATT> gatt)
    : hci_(hci),
      request_timeout_ms_(kLECreateConnectionTimeoutMs),
      dispatcher_(async_get_default_dispatcher()),
      device_cache_(device_cache),
      data_domain_(data_domain),
      gatt_(gatt),
      connector_(connector),
      weak_ptr_factory_(this) {
  ZX_DEBUG_ASSERT(dispatcher_);
  ZX_DEBUG_ASSERT(device_cache_);
  ZX_DEBUG_ASSERT(data_domain_);
  ZX_DEBUG_ASSERT(gatt_);
  ZX_DEBUG_ASSERT(hci_);
  ZX_DEBUG_ASSERT(connector_);

  auto self = weak_ptr_factory_.GetWeakPtr();

  // TODO(armansito): Setting this up here means that the
  // ClassicConnectionManager won't be able to listen to the same event. So this
  // event either needs to be handled elsewhere OR we make hci::CommandChannel
  // support registering multiple handlers for the same event.
  disconn_cmpl_handler_id_ = hci->command_channel()->AddEventHandler(
      hci::kDisconnectionCompleteEventCode,
      [self](const auto& event) {
        if (self)
          self->OnDisconnectionComplete(event);
      },
      dispatcher_);

  conn_update_cmpl_handler_id_ = hci_->command_channel()->AddLEMetaEventHandler(
      hci::kLEConnectionUpdateCompleteSubeventCode,
      [self](const auto& event) {
        if (self)
          self->OnLEConnectionUpdateComplete(event);
      },
      dispatcher_);
}

LowEnergyConnectionManager::~LowEnergyConnectionManager() {
  hci_->command_channel()->RemoveEventHandler(conn_update_cmpl_handler_id_);
  hci_->command_channel()->RemoveEventHandler(disconn_cmpl_handler_id_);

  bt_log(TRACE, "gap-le", "connection manager shutting down");

  weak_ptr_factory_.InvalidateWeakPtrs();

  // This will cancel any pending request.
  if (connector_->request_pending()) {
    connector_->Cancel();
  }

  // Clear |pending_requests_| and notify failure.
  for (auto& iter : pending_requests_) {
    iter.second.NotifyCallbacks(hci::Status(common::HostError::kFailed),
                                [] { return nullptr; });
  }
  pending_requests_.clear();

  // Clean up all connections.
  for (auto& iter : connections_) {
    CleanUpConnection(std::move(iter.second));
  }

  connections_.clear();
}

bool LowEnergyConnectionManager::Connect(const std::string& device_identifier,
                                         ConnectionResultCallback callback) {
  if (!connector_) {
    bt_log(WARN, "gap-le", "connect called during shutdown!");
    return false;
  }

  RemoteDevice* peer = device_cache_->FindDeviceById(device_identifier);
  if (!peer) {
    bt_log(WARN, "gap-le", "device not found (id: %s)",
           device_identifier.c_str());
    return false;
  }

  if (peer->technology() == TechnologyType::kClassic) {
    bt_log(ERROR, "gap-le", "device does not support LE: %s",
           peer->ToString().c_str());
    return false;
  }

  if (!peer->connectable()) {
    bt_log(ERROR, "gap-le", "device not connectable: %s",
           peer->ToString().c_str());
    return false;
  }

  // If we are already waiting to connect to |device_identifier| then we store
  // |callback| to be processed after the connection attempt completes (in
  // either success of failure).
  auto pending_iter = pending_requests_.find(device_identifier);
  if (pending_iter != pending_requests_.end()) {
    ZX_DEBUG_ASSERT(connections_.find(device_identifier) == connections_.end());
    ZX_DEBUG_ASSERT(connector_->request_pending());

    pending_iter->second.AddCallback(std::move(callback));
    return true;
  }

  // If there is already an active connection then we add a new reference and
  // succeed.
  auto conn_ref = AddConnectionRef(device_identifier);
  if (conn_ref) {
    async::PostTask(dispatcher_, [conn_ref = std::move(conn_ref),
                                  callback = std::move(callback)]() mutable {
      // Do not report success if the link has been disconnected (e.g. via
      // Disconnect() or other circumstances).
      if (!conn_ref->active()) {
        bt_log(TRACE, "gap-le", "link disconnected, ref is inactive");
        callback(hci::Status(common::HostError::kFailed), nullptr);
      } else {
        callback(hci::Status(), std::move(conn_ref));
      }
    });

    return true;
  }

  peer->MutLe().SetConnectionState(
      RemoteDevice::ConnectionState::kInitializing);
  pending_requests_[device_identifier] =
      PendingRequestData(peer->address(), std::move(callback));

  TryCreateNextConnection();

  return true;
}

bool LowEnergyConnectionManager::Disconnect(
    const std::string& device_identifier) {
  auto iter = connections_.find(device_identifier);
  if (iter == connections_.end()) {
    bt_log(WARN, "gap-le", "device not connected (id: %s)",
           device_identifier.c_str());
    return false;
  }

  // Remove the connection state from the internal map right away.
  auto conn = std::move(iter->second);
  connections_.erase(iter);

  bt_log(INFO, "gap-le", "disconnecting link: %s",
         conn->link()->ToString().c_str());
  CleanUpConnection(std::move(conn));
  return true;
}

LowEnergyConnectionRefPtr
LowEnergyConnectionManager::RegisterRemoteInitiatedLink(
    hci::ConnectionPtr link) {
  ZX_DEBUG_ASSERT(link);
  bt_log(TRACE, "gap-le", "new remote-initiated link (local addr: %s): %s",
         link->local_address().ToString().c_str(), link->ToString().c_str());

  RemoteDevice* peer = UpdateRemoteDeviceWithLink(*link);

  // TODO(armansito): Use own address when storing the connection (NET-321).
  // Currently this will refuse the connection and disconnect the link if |peer|
  // is already connected to us by a different local address.
  auto conn_ref = InitializeConnection(peer->identifier(), std::move(link));
  if (conn_ref) {
    peer->MutLe().SetConnectionState(RemoteDevice::ConnectionState::kConnected);
  }
  return conn_ref;
}

void LowEnergyConnectionManager::SetPairingDelegate(
    fxl::WeakPtr<PairingDelegate> delegate) {
  // TODO(armansito): Add a test case for this once NET-1179 is done.
  pairing_delegate_ = delegate;

  // Tell existing connections to abort ongoing pairing procedures. The new
  // delegate will receive calls to PairingDelegate::CompletePairing, unless it
  // is null.
  for (auto& iter : connections_) {
    iter.second->ResetPairingState(delegate
                                       ? delegate->io_capability()
                                       : sm::IOCapability::kNoInputNoOutput);
  }
}

void LowEnergyConnectionManager::SetConnectionParametersCallbackForTesting(
    ConnectionParametersCallback callback) {
  test_conn_params_cb_ = std::move(callback);
}

void LowEnergyConnectionManager::SetDisconnectCallbackForTesting(
    DisconnectCallback callback) {
  test_disconn_cb_ = std::move(callback);
}

void LowEnergyConnectionManager::ReleaseReference(
    LowEnergyConnectionRef* conn_ref) {
  ZX_DEBUG_ASSERT(conn_ref);

  auto iter = connections_.find(conn_ref->device_identifier());
  ZX_DEBUG_ASSERT(iter != connections_.end());

  iter->second->DropRef(conn_ref);
  if (iter->second->ref_count() != 0u)
    return;

  // Move the connection object before erasing the entry.
  auto conn = std::move(iter->second);
  connections_.erase(iter);

  bt_log(INFO, "gap-le", "all refs dropped on connection: %s",
         conn->link()->ToString().c_str());
  CleanUpConnection(std::move(conn));
}

void LowEnergyConnectionManager::TryCreateNextConnection() {
  // There can only be one outstanding LE Create Connection request at a time.
  if (connector_->request_pending()) {
    bt_log(TRACE, "gap-le", "HCI_LE_Create_Connection command pending");
    return;
  }

  // TODO(armansito): Perform either the General or Auto Connection
  // Establishment procedure here (see NET-187).

  if (pending_requests_.empty()) {
    bt_log(SPEW, "gap-le", "no pending requests remaining");

    // TODO(armansito): Unpause discovery and disable background scanning if
    // there aren't any devices to auto-connect to.
    return;
  }

  for (auto& iter : pending_requests_) {
    const auto& next_device_addr = iter.second.address();
    RemoteDevice* peer = device_cache_->FindDeviceByAddress(next_device_addr);
    if (peer) {
      RequestCreateConnection(peer);
      break;
    }

    bt_log(TRACE, "gap-le", "deferring connection attempt for device: %s",
           next_device_addr.ToString().c_str());

    // TODO(armansito): For now the requests for this device won't complete
    // until the next device discovery. This will no longer be an issue when we
    // use background scanning (see NET-187).
  }
}

void LowEnergyConnectionManager::RequestCreateConnection(RemoteDevice* peer) {
  ZX_DEBUG_ASSERT(peer);

  // During the initial connection to a peripheral we use the initial high
  // duty-cycle parameters to ensure that initiating procedures (bonding,
  // encryption setup, service discovery) are completed quickly. Once these
  // procedures are complete, we will change the connection interval to the
  // peripheral's preferred connection parameters (see v5.0, Vol 3, Part C,
  // Section 9.3.12).

  // TODO(armansito): Initiate the connection using the cached preferred
  // connection parameters if we are bonded.
  hci::LEPreferredConnectionParameters initial_params(
      kLEInitialConnIntervalMin, kLEInitialConnIntervalMax, 0,
      hci::defaults::kLESupervisionTimeout);

  auto self = weak_ptr_factory_.GetWeakPtr();
  auto status_cb = [self, device_id = peer->identifier()](hci::Status status,
                                                          auto link) {
    if (self)
      self->OnConnectResult(device_id, status, std::move(link));
  };

  // We set the scan window and interval to the same value for continuous
  // scanning.
  // TODO(armansito): Use one of the resolvable address types here.
  connector_->CreateConnection(hci::LEOwnAddressType::kPublic,
                               false /* use_whitelist */, peer->address(),
                               kLEScanFastInterval, kLEScanFastInterval,
                               initial_params, status_cb, request_timeout_ms_);
}

LowEnergyConnectionRefPtr LowEnergyConnectionManager::InitializeConnection(
    const std::string& device_id, std::unique_ptr<hci::Connection> link) {
  ZX_DEBUG_ASSERT(link);
  ZX_DEBUG_ASSERT(link->ll_type() == hci::Connection::LinkType::kLE);

  // TODO(armansito): For now reject having more than one link with the same
  // peer. This should change once this has more context on the local
  // destination for remote initiated connections (see NET-321).
  if (connections_.find(device_id) != connections_.end()) {
    bt_log(TRACE, "gap-le", "multiple links from device; connection refused");
    link->Close();
    return nullptr;
  }

  // Add the connection to the L2CAP table. Incoming data will be buffered until
  // the channels are open.
  auto self = weak_ptr_factory_.GetWeakPtr();
  auto conn_param_update_cb = [self, handle = link->handle(),
                               device_id](const auto& params) {
    if (self) {
      self->OnNewLEConnectionParams(device_id, handle, params);
    }
  };

  auto link_error_cb = [self, device_id] {
    bt_log(TRACE, "gap", "link error received from L2CAP");
    if (self) {
      self->Disconnect(device_id);
    }
  };

  // Initialize connection.
  auto conn = std::make_unique<internal::LowEnergyConnection>(
      device_id, std::move(link), dispatcher_, weak_ptr_factory_.GetWeakPtr());
  conn->InitializeFixedChannels(data_domain_, gatt_,
                                std::move(conn_param_update_cb),
                                std::move(link_error_cb));

  auto first_ref = conn->AddRef();
  connections_[device_id] = std::move(conn);

  // TODO(armansito): Should complete a few more things before returning the
  // connection:
  //    1. If this is the first time we connected to this device:
  //      a. Obtain LE remote features
  //      b. If master, obtain Peripheral Preferred Connection Parameters via
  //         GATT if available
  //      c. Initiate name discovery over GATT if complete name is unknown
  //      e. If master, allow slave to initiate procedures (service discovery,
  //         encryption setup, etc) for kLEConnectionPauseCentralMs before
  //         updating the connection parameters to the slave's preferred values.

  return first_ref;
}

LowEnergyConnectionRefPtr LowEnergyConnectionManager::AddConnectionRef(
    const std::string& device_identifier) {
  auto iter = connections_.find(device_identifier);
  if (iter == connections_.end())
    return nullptr;

  return iter->second->AddRef();
}

void LowEnergyConnectionManager::CleanUpConnection(
    std::unique_ptr<internal::LowEnergyConnection> conn, bool close_link) {
  ZX_DEBUG_ASSERT(conn);

  // Mark the peer device as no longer connected.
  RemoteDevice* peer = device_cache_->FindDeviceById(conn->id());
  ZX_DEBUG_ASSERT(peer);
  peer->MutLe().SetConnectionState(
      RemoteDevice::ConnectionState::kNotConnected);

  // Clean up GATT profile.
  gatt_->RemoveConnection(conn->id());

  // Remove the connection from L2CAP. This will invalidate all channels that
  // are associated with this link.
  data_domain_->RemoveConnection(conn->handle());

  if (!close_link) {
    // Mark the connection as already closed so that hci::Connection::Close()
    // doesn't send HCI_Disconnect to the controller.
    //
    // |close_link| is expected to be false only when this method is called due
    // to a disconnection that was not requested by the local host.
    conn->link()->set_closed();
  }

  // The |conn| is destroyed when it goes out of scope.
}

void LowEnergyConnectionManager::RegisterLocalInitiatedLink(
    std::unique_ptr<hci::Connection> link) {
  ZX_DEBUG_ASSERT(link);
  ZX_DEBUG_ASSERT(link->ll_type() == hci::Connection::LinkType::kLE);
  bt_log(INFO, "gap-le", "new connection %s", link->ToString().c_str());

  RemoteDevice* peer = UpdateRemoteDeviceWithLink(*link);

  // Initialize the connection  and obtain the initial reference.
  // This reference lasts until this method returns to prevent it from dropping
  // to 0 due to an unclaimed reference while notifying pending callbacks and
  // listeners below.
  auto first_ref = InitializeConnection(peer->identifier(), std::move(link));

  // We take care never to initiate more than one connection to the same peer.
  ZX_DEBUG_ASSERT(first_ref);

  auto conn_iter = connections_.find(peer->identifier());
  ZX_DEBUG_ASSERT(conn_iter != connections_.end());

  // For now, jump to the initialized state.
  peer->MutLe().SetConnectionState(RemoteDevice::ConnectionState::kConnected);

  auto iter = pending_requests_.find(peer->identifier());
  if (iter != pending_requests_.end()) {
    // Remove the entry from |pending_requests_| before notifying the callbacks.
    auto pending_req_data = std::move(iter->second);
    pending_requests_.erase(iter);

    pending_req_data.NotifyCallbacks(
        hci::Status(), [&conn_iter] { return conn_iter->second->AddRef(); });
  }

  // Release the extra reference before attempting the next connection. This
  // will disconnect the link if no callback retained its reference.
  first_ref = nullptr;

  ZX_DEBUG_ASSERT(!connector_->request_pending());
  TryCreateNextConnection();
}

RemoteDevice* LowEnergyConnectionManager::UpdateRemoteDeviceWithLink(
    const hci::Connection& link) {
  RemoteDevice* peer = device_cache_->FindDeviceByAddress(link.peer_address());
  if (!peer) {
    peer =
        device_cache_->NewDevice(link.peer_address(), true /* connectable */);
  }
  peer->MutLe().SetConnectionParameters(link.low_energy_parameters());
  return peer;
}

void LowEnergyConnectionManager::OnConnectResult(
    const std::string& device_identifier, hci::Status status,
    hci::ConnectionPtr link) {
  ZX_DEBUG_ASSERT(connections_.find(device_identifier) == connections_.end());

  if (status) {
    bt_log(TRACE, "gap-le", "connection request successful");
    RegisterLocalInitiatedLink(std::move(link));
    return;
  }

  // The request failed or timed out.
  bt_log(ERROR, "gap-le", "failed to connect to device (id: %s)",
         device_identifier.c_str());
  RemoteDevice* dev = device_cache_->FindDeviceById(device_identifier);
  ZX_ASSERT(dev);
  dev->MutLe().SetConnectionState(RemoteDevice::ConnectionState::kNotConnected);

  // Notify the matching pending callbacks about the failure.
  auto iter = pending_requests_.find(device_identifier);
  ZX_DEBUG_ASSERT(iter != pending_requests_.end());

  // Remove the entry from |pending_requests_| before notifying callbacks.
  auto pending_req_data = std::move(iter->second);
  pending_requests_.erase(iter);
  pending_req_data.NotifyCallbacks(status, [] { return nullptr; });

  // Process the next pending attempt.
  ZX_DEBUG_ASSERT(!connector_->request_pending());
  TryCreateNextConnection();
}

void LowEnergyConnectionManager::OnDisconnectionComplete(
    const hci::EventPacket& event) {
  ZX_DEBUG_ASSERT(event.event_code() == hci::kDisconnectionCompleteEventCode);
  const auto& params =
      event.view().payload<hci::DisconnectionCompleteEventParams>();
  hci::ConnectionHandle handle = le16toh(params.connection_handle);

  if (params.status != hci::StatusCode::kSuccess) {
    bt_log(TRACE, "gap-le",
           "HCI connection event received with error (status: \"%s\", handle: "
           "%#.4x",
           hci::StatusCodeToString(params.status).c_str(), handle);
    return;
  }

  bt_log(INFO, "gap-le",
         "link disconnected - status: \"%s\", handle: %#.4x, reason: %#.2x",
         hci::StatusCodeToString(params.status).c_str(), handle, params.reason);

  if (test_disconn_cb_)
    test_disconn_cb_(handle);

  // See if we can find a connection with a matching handle by walking the
  // connections list.
  auto iter = FindConnection(handle);
  if (iter == connections_.end()) {
    bt_log(TRACE, "gap-le", "unknown connection handle: %#.4x", handle);
    return;
  }

  // Found the connection. Remove the entry from |connections_| before notifying
  // the "closed" handlers.
  auto conn = std::move(iter->second);
  connections_.erase(iter);

  ZX_DEBUG_ASSERT(conn->ref_count());

  // The connection is already closed, so no need to send HCI_Disconnect.
  CleanUpConnection(std::move(conn), false /* close_link */);
}

void LowEnergyConnectionManager::OnLEConnectionUpdateComplete(
    const hci::EventPacket& event) {
  ZX_DEBUG_ASSERT(event.event_code() == hci::kLEMetaEventCode);
  ZX_DEBUG_ASSERT(
      event.view().payload<hci::LEMetaEventParams>().subevent_code ==
      hci::kLEConnectionUpdateCompleteSubeventCode);

  auto payload =
      event.le_event_params<hci::LEConnectionUpdateCompleteSubeventParams>();
  ZX_ASSERT(payload);
  hci::ConnectionHandle handle = le16toh(payload->connection_handle);

  if (payload->status != hci::StatusCode::kSuccess) {
    bt_log(WARN, "gap-le",
           "HCI LE Connection Update Complete event with error "
           "(status: %#.2x, handle: %#.4x)",
           payload->status, handle);
    return;
  }

  auto iter = FindConnection(handle);
  if (iter == connections_.end()) {
    bt_log(TRACE, "gap-le",
           "conn. parameters received for unknown link (handle: %#.4x)",
           handle);
    return;
  }

  const auto& conn = *iter->second;
  ZX_DEBUG_ASSERT(conn.handle() == handle);

  bt_log(INFO, "gap-le", "conn. parameters updated (id: %s, handle: %#.4x)",
         conn.id().c_str(), handle);
  hci::LEConnectionParameters params(le16toh(payload->conn_interval),
                                     le16toh(payload->conn_latency),
                                     le16toh(payload->supervision_timeout));
  conn.link()->set_low_energy_parameters(params);

  RemoteDevice* peer = device_cache_->FindDeviceById(conn.id());
  if (!peer) {
    bt_log(ERROR, "gap-le", "conn. parameters updated for unknown peer!");
    return;
  }

  peer->MutLe().SetConnectionParameters(params);

  if (test_conn_params_cb_)
    test_conn_params_cb_(*peer);
}

void LowEnergyConnectionManager::OnNewLEConnectionParams(
    const std::string& device_identifier, hci::ConnectionHandle handle,
    const hci::LEPreferredConnectionParameters& params) {
  bt_log(TRACE, "gap-le", "conn. parameters received (handle: %#.4x)", handle);

  RemoteDevice* peer = device_cache_->FindDeviceById(device_identifier);
  if (!peer) {
    bt_log(ERROR, "gap-le", "conn. parameters received from unknown peer!");
    return;
  }

  peer->MutLe().SetPreferredConnectionParameters(params);

  // Use the new parameters if we're not performing service discovery or
  // bonding.
  if (peer->le()->connected()) {
    UpdateConnectionParams(handle, params);
  }
}

void LowEnergyConnectionManager::UpdateConnectionParams(
    hci::ConnectionHandle handle,
    const hci::LEPreferredConnectionParameters& params) {
  bt_log(TRACE, "gap-le", "updating conn. parameters (handle: %#.4x)", handle);
  auto command = hci::CommandPacket::New(
      hci::kLEConnectionUpdate, sizeof(hci::LEConnectionUpdateCommandParams));
  auto event_params =
      command->mutable_view()
          ->mutable_payload<hci::LEConnectionUpdateCommandParams>();

  event_params->connection_handle = htole16(handle);
  event_params->conn_interval_min = htole16(params.min_interval());
  event_params->conn_interval_max = htole16(params.max_interval());
  event_params->conn_latency = htole16(params.max_latency());
  event_params->supervision_timeout = htole16(params.supervision_timeout());
  event_params->minimum_ce_length = 0x0000;
  event_params->maximum_ce_length = 0x0000;

  auto status_cb = [handle](auto id, const hci::EventPacket& event) {
    ZX_DEBUG_ASSERT(event.event_code() == hci::kCommandStatusEventCode);

    // Warn if the command failed.
    hci_is_error(event, TRACE, "gap-le",
                 "controller rejected connection parameters");
  };

  hci_->command_channel()->SendCommand(std::move(command), dispatcher_,
                                       status_cb, hci::kCommandStatusEventCode);
}

LowEnergyConnectionManager::ConnectionMap::iterator
LowEnergyConnectionManager::FindConnection(hci::ConnectionHandle handle) {
  auto iter = connections_.begin();
  for (; iter != connections_.end(); ++iter) {
    const auto& conn = *iter->second;
    if (conn.handle() == handle)
      break;
  }
  return iter;
}

}  // namespace gap
}  // namespace btlib
