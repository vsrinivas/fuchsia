// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "low_energy_connection_manager.h"

#include "garnet/drivers/bluetooth/lib/hci/defaults.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"
#include "garnet/drivers/bluetooth/lib/l2cap/channel_manager.h"

#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "remote_device.h"
#include "remote_device_cache.h"

namespace bluetooth {
namespace gap {

LowEnergyConnectionRef::LowEnergyConnectionRef(const std::string& device_id,
                                               fxl::WeakPtr<LowEnergyConnectionManager> manager)
    : active_(true), device_id_(device_id), manager_(manager) {
  FXL_DCHECK(!device_id_.empty());
  FXL_DCHECK(manager_);
}

LowEnergyConnectionRef::~LowEnergyConnectionRef() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  if (active_) Release();
};

void LowEnergyConnectionRef::Release() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(active_);
  active_ = false;
  if (manager_) manager_->ReleaseReference(this);
}

void LowEnergyConnectionRef::MarkClosed() {
  active_ = false;
  if (closed_cb_) closed_cb_();
}

void LowEnergyConnectionManager::ConnectionState::CloseRefs() {
  for (auto* conn_ref : refs) conn_ref->MarkClosed();
}

LowEnergyConnectionManager::PendingRequestData::PendingRequestData(
    const common::DeviceAddress& address, const ConnectionResultCallback& first_callback)
    : address_(address), callbacks_{first_callback} {}

void LowEnergyConnectionManager::PendingRequestData::NotifyCallbacks(hci::Status status,
                                                                     const RefFunc& func) {
  FXL_DCHECK(!callbacks_.empty());
  for (const auto& callback : callbacks_) {
    callback(status, func());
  }
}

LowEnergyConnectionManager::LowEnergyConnectionManager(Mode /* mode */,
                                                       fxl::RefPtr<hci::Transport> hci,
                                                       RemoteDeviceCache* device_cache,
                                                       l2cap::ChannelManager* l2cap,
                                                       int64_t request_timeout_ms)
    : hci_(hci),
      request_timeout_ms_(request_timeout_ms),
      task_runner_(fsl::MessageLoop::GetCurrent()->task_runner()),
      device_cache_(device_cache),
      l2cap_(l2cap),
      next_listener_id_(1),
      weak_ptr_factory_(this) {
  FXL_DCHECK(task_runner_);
  FXL_DCHECK(device_cache_);
  FXL_DCHECK(l2cap_);
  FXL_DCHECK(hci_);

  // TODO(armansito): Use |mode| initialize the |connector_| when we support the extended feature.
  // For now |mode| is ignored.
  auto self = weak_ptr_factory_.GetWeakPtr();
  connector_ = std::make_unique<hci::LowEnergyConnector>(hci_, task_runner_, [self](auto conn) {
    if (self) self->OnConnectionCreated(std::move(conn));
  });

  // TODO(armansito): Setting this up here means that the ClassicConnectionManager won't be able to
  // listen to the same event. So this event either needs to be handled elsewhere OR we make
  // hci::CommandChannel support registering multiple handlers for the same event.
  event_handler_id_ =
      hci->command_channel()->AddEventHandler(hci::kDisconnectionCompleteEventCode,
                                              [self](const auto& event) {
                                                if (self) self->OnDisconnectionComplete(event);
                                              },
                                              task_runner_);
}

LowEnergyConnectionManager::~LowEnergyConnectionManager() {
  hci_->command_channel()->RemoveEventHandler(event_handler_id_);

  FXL_VLOG(1) << "gap: LowEnergyConnectionManager: shutting down";

  weak_ptr_factory_.InvalidateWeakPtrs();

  // This will cancel any pending request.
  connector_ = nullptr;

  // Clear |pending_requests_| and notify failure.
  for (auto& iter : pending_requests_) {
    // TODO(armansito): Use our own error code for errors that don't come from the controller
    // (such as this and command timeout).
    iter.second.NotifyCallbacks(hci::Status::kHardwareFailure, [] { return nullptr; });
  }
  pending_requests_.clear();

  // Clean up all connections.
  for (auto& iter : connections_) {
    auto& conn_state = iter.second;
    CleanUpConnectionState(&conn_state);
  }

  connections_.clear();
}

bool LowEnergyConnectionManager::Connect(const std::string& device_identifier,
                                         const ConnectionResultCallback& callback) {
  if (!connector_) {
    FXL_LOG(WARNING) << "gap: LowEnergyConnectionManager: Connect called during shutdown!";
    return false;
  }

  RemoteDevice* peer = device_cache_->FindDeviceById(device_identifier);
  if (!peer) {
    FXL_LOG(WARNING) << "gap: LowEnergyConnectionManager: device not found (id: "
                     << device_identifier << ")";
    return false;
  }

  if (peer->technology() == TechnologyType::kClassic) {
    FXL_LOG(ERROR) << "gap: LowEnergyConnectionManager: device does not support LE: "
                   << peer->ToString();
    return false;
  }

  if (!peer->connectable()) {
    FXL_LOG(ERROR) << "gap: LowEnergyConnectionManager: device not connectable: "
                   << peer->ToString();
    return false;
  }

  // If we are already waiting to connect to |device_identifier| then we store |callback| to be
  // processed after the connection attempt completes (in either success of failure).
  auto pending_iter = pending_requests_.find(device_identifier);
  if (pending_iter != pending_requests_.end()) {
    FXL_DCHECK(connections_.find(device_identifier) == connections_.end());
    FXL_DCHECK(connector_->request_pending());

    pending_iter->second.AddCallback(callback);
    return true;
  }

  // If there is already an active connection then we add a new reference and succeed.
  auto conn_ref = AddConnectionRef(device_identifier);
  if (conn_ref) {
    task_runner_->PostTask(
        fxl::MakeCopyable([ conn_ref = std::move(conn_ref), callback ]() mutable {
          // Do not report success if the link has been disconnected (e.g. via Disconnect() or
          // other circumstances).
          if (!conn_ref->active()) {
            FXL_VLOG(1) << "gap: LowEnergyConnectionManager: Link disconnected, ref is inactive";

            // TODO(armansito): Use a non-HCI error code for this.
            callback(hci::Status::kConnectionFailedToBeEstablished, nullptr);
          } else {
            callback(hci::Status::kSuccess, std::move(conn_ref));
          }
        }));

    return true;
  }

  pending_requests_[device_identifier] = PendingRequestData(peer->address(), callback);

  TryCreateNextConnection();

  return true;
}

bool LowEnergyConnectionManager::Disconnect(const std::string& device_identifier) {
  auto iter = connections_.find(device_identifier);
  if (iter == connections_.end()) {
    FXL_LOG(WARNING) << "gap: LowEnergyConnectionManager: device not connected (id: "
                     << device_identifier << ")";
    return false;
  }

  // Remove the connection state from the internal map right away.
  auto conn_state = std::move(iter->second);
  connections_.erase(iter);

  FXL_DCHECK(conn_state.conn);
  FXL_DCHECK(!conn_state.refs.empty());

  FXL_LOG(INFO) << "gap: LowEnergyConnectionManager: disconnecting link: "
                << conn_state.conn->ToString();

  CleanUpConnectionState(&conn_state);
  return true;
}

LowEnergyConnectionManager::ListenerId LowEnergyConnectionManager::AddListener(
    const ConnectionCallback& callback) {
  FXL_DCHECK(callback);
  FXL_DCHECK(listeners_.find(next_listener_id_) == listeners_.end());

  auto id = next_listener_id_++;
  FXL_DCHECK(next_listener_id_);
  FXL_DCHECK(id);
  listeners_[id] = callback;

  return id;
}

void LowEnergyConnectionManager::RemoveListener(ListenerId id) {
  listeners_.erase(id);
}

void LowEnergyConnectionManager::SetDisconnectCallbackForTesting(
    const DisconnectCallback& callback) {
  test_disconn_cb_ = callback;
}

void LowEnergyConnectionManager::ReleaseReference(LowEnergyConnectionRef* conn_ref) {
  FXL_DCHECK(conn_ref);

  auto iter = connections_.find(conn_ref->device_identifier());
  FXL_DCHECK(iter != connections_.end());

  auto& conn_state = iter->second;
  FXL_DCHECK(conn_state.conn);

  // Drop the reference from the connection state.
  __UNUSED size_t res = conn_state.refs.erase(conn_ref);
  FXL_DCHECK(res == 1u) << "ReleaseReference called on bad |conn_ref|!";
  FXL_VLOG(1) << fxl::StringPrintf(
      "gap: LowEnergyConnectionManager: dropped ref (handle: 0x%04x, refs: %lu)",
      conn_state.conn->handle(), conn_state.refs.size());

  if (!conn_state.refs.empty()) {
    return;
  }

  FXL_LOG(INFO) << "gap: LowEnergyConnectionManager: all refs dropped on connection: "
                << conn_state.conn->ToString();

  auto conn_state_moved = std::move(conn_state);
  connections_.erase(iter);

  CleanUpConnectionState(&conn_state_moved);
}

void LowEnergyConnectionManager::TryCreateNextConnection() {
  // There can only be one outstanding LE Create Connection request at a time.
  if (connector_->request_pending()) {
    FXL_VLOG(1) << "gap: LowEnergyConnectionManager: HCI_LE_Create_Connection command pending";
    return;
  }

  // TODO(armansito): Perform either the General or Auto Connection Establishment procedure here
  // (see NET-187).

  if (pending_requests_.empty()) {
    FXL_VLOG(2) << "gap: LowEnergyConnectionManager: No pending requests remaining";

    // TODO(armansito): Unpause discovery and disable background scanning if there aren't any
    // devices to auto-connect to.
    return;
  }

  for (auto& iter : pending_requests_) {
    const auto& next_device_addr = iter.second.address();
    RemoteDevice* peer = device_cache_->FindDeviceByAddress(next_device_addr);
    if (peer) {
      RequestCreateConnection(peer);
      break;
    }

    FXL_VLOG(1) << "gap: LowEnergyConnectionManager: Deferring connection attempt for device: "
                << next_device_addr.ToString();

    // TODO(armansito): For now the requests for this device won't complete until the next device
    // discovery. This will no longer be an issue when we use background scanning (see NET-187).
  }
}

void LowEnergyConnectionManager::RequestCreateConnection(RemoteDevice* peer) {
  FXL_DCHECK(peer);

  // TODO(armansito): It should be possible to obtain connection parameters dynamically:
  //
  //    1. If |peer| has cached parameters from a previous connection, use those (already
  //       implemented).
  //    2. If |peer| has specified its preferred connection parameters while advertising, use those.
  //    3. Use any dynamically specified default connection parameters, once this system has an API
  //       for it.

  // During the initial connection to a peripheral we use the initial high duty-cycle parameters
  // to ensure that initiating procedures (bonding, encryption setup, service discovery) are
  // completed quickly. Once these procedures are complete, we will change the connection interval
  // to the peripheral's preferred connection parameters (see v5.0, Vol 3, Part C, Section 9.3.12).
  //
  // TODO(armansito): For a device that was previously connected/bonded we should use the preferred
  // parameters right away.
  auto* cached_params = peer->le_connection_params();
  hci::Connection::LowEnergyParameters initial_params(
      kLEInitialConnIntervalMin, kLEInitialConnIntervalMax,
      cached_params ? cached_params->interval() : 0, cached_params ? cached_params->latency() : 0,
      cached_params ? cached_params->supervision_timeout() : hci::defaults::kLESupervisionTimeout);

  auto self = weak_ptr_factory_.GetWeakPtr();
  auto result_cb = [ self, device_id = peer->identifier() ](auto result, auto status) {
    if (self) self->OnConnectResult(device_id, result, status);
  };

  // We set the scan window and interval to the same value for continuous scanning.
  // TODO(armansito): Use one of the resolvable address types here.
  connector_->CreateConnection(hci::LEOwnAddressType::kPublic, false /* use_whitelist */,
                               peer->address(), kLEScanFastInterval, kLEScanFastInterval,
                               initial_params, result_cb, request_timeout_ms_);
}

LowEnergyConnectionRefPtr LowEnergyConnectionManager::InitializeConnection(
    const std::string& device_identifier, std::unique_ptr<hci::Connection> connection) {
  FXL_DCHECK(connections_.find(device_identifier) == connections_.end());

  LowEnergyConnectionRefPtr conn_ref(
      new LowEnergyConnectionRef(device_identifier, weak_ptr_factory_.GetWeakPtr()));

  ConnectionState state;
  state.conn = std::move(connection);
  state.refs.insert(conn_ref.get());
  connections_[device_identifier] = std::move(state);

  return conn_ref;
}

LowEnergyConnectionRefPtr LowEnergyConnectionManager::AddConnectionRef(
    const std::string& device_identifier) {
  auto iter = connections_.find(device_identifier);
  if (iter == connections_.end()) return nullptr;

  LowEnergyConnectionRefPtr conn_ref(
      new LowEnergyConnectionRef(device_identifier, weak_ptr_factory_.GetWeakPtr()));
  iter->second.refs.insert(conn_ref.get());

  FXL_VLOG(1) << fxl::StringPrintf(
      "gap: LowEnergyConnectionManager: added ref (handle: 0x%04x, refs: %lu)",
      iter->second.conn->handle(), iter->second.refs.size());

  return conn_ref;
}

void LowEnergyConnectionManager::CleanUpConnectionState(ConnectionState* conn_state) {
  FXL_DCHECK(conn_state);
  FXL_DCHECK(conn_state->conn);

  // This will notify all open L2CAP channels about the severed link.
  l2cap_->Unregister(conn_state->conn->handle());

  // Close the link if it marked as open.
  conn_state->conn->Close();

  // Notify all active references that the link is gone. This will synchronously notify all refs.
  conn_state->CloseRefs();
}

void LowEnergyConnectionManager::OnConnectionCreated(std::unique_ptr<hci::Connection> connection) {
  FXL_DCHECK(connection);
  FXL_DCHECK(connection->ll_type() == hci::Connection::LinkType::kLE);
  FXL_LOG(INFO) << "gap: LowEnergyDiscoveryManager: new connection: " << connection->ToString();

  RemoteDevice* peer = device_cache_->StoreLowEnergyConnection(
      connection->peer_address(), connection->ll_type(), connection->low_energy_parameters());

  // Add the connection to the connection map and obtain the initial reference. This reference lasts
  // until this method returns to prevent it from dropping to 0 due to an unclaimed reference while
  // notifying pending callbacks and listeners below.
  auto conn_ptr = connection.get();
  auto conn_ref = InitializeConnection(peer->identifier(), std::move(connection));

  // Add the connection the L2CAP table. Incoming data will be buffered until the channels are open.
  l2cap_->Register(conn_ptr->handle(), conn_ptr->ll_type(), conn_ptr->role());

  // TODO(armansito): Listeners and pending request handlers should not be called yet since there
  // are still a few more things to complete:
  //    1. Initialize SMP bearer
  //    2. Initialize ATT bearer
  //    3. If this is the first time we connected to this device:
  //      a. Obtain LE remote features
  //      a. If master, obtain Peripheral Preferred Connection Parameters via GATT if available
  //      b. Initiate name discovery over GATT if complete name is unknown
  //      d. Initiate service discovery over GATT
  //      c. If master, update connection parameters to the slave's preferred values after
  //         kLEConnectionPauseCentralMs, if any.

  auto iter = pending_requests_.find(peer->identifier());
  if (iter != pending_requests_.end()) {
    // Remove the entry from |pending_requests_| before notifying the callbacks.
    auto pending_req_data = std::move(iter->second);
    pending_requests_.erase(iter);

    pending_req_data.NotifyCallbacks(hci::Status::kSuccess, [this, peer] {
      auto conn_ref = AddConnectionRef(peer->identifier());
      FXL_CHECK(conn_ref);
      return conn_ref;
    });
  }

  // Notify each listener with a unique reference.
  for (const auto& iter : listeners_) {
    auto conn_ref = AddConnectionRef(peer->identifier());
    FXL_DCHECK(conn_ref);

    iter.second(std::move(conn_ref));
  }

  // Release the extra reference before attempting the next connection. This will disconnect the
  // link if no callback or listener retained its reference.
  conn_ref = nullptr;

  FXL_DCHECK(!connector_->request_pending());
  TryCreateNextConnection();
}

void LowEnergyConnectionManager::OnConnectResult(const std::string& device_identifier,
                                                 hci::LowEnergyConnector::Result result,
                                                 hci::Status status) {
  FXL_DCHECK(connections_.find(device_identifier) == connections_.end());

  if (result == hci::LowEnergyConnector::Result::kSuccess) {
    FXL_VLOG(1) << "gap: LowEnergyConnectionManager: LE connection request successful";

    // We'll complete the request when we obtain a Connection object in OnConnectionCreated().
    return;
  }

  FXL_LOG(ERROR) << "gap: LowEnergyConnectionManager: Failed to connect to device (id: "
                 << device_identifier << ")";

  // The request failed or timed out. Notify the matching pending callbacks about the failure and
  // process the next connection attempt.
  auto iter = pending_requests_.find(device_identifier);
  FXL_DCHECK(iter != pending_requests_.end());

  // Remove the entry from |pending_requests_| before notifying callbacks.
  auto pending_req_data = std::move(iter->second);
  pending_requests_.erase(iter);
  pending_req_data.NotifyCallbacks(status, [] { return nullptr; });

  FXL_DCHECK(!connector_->request_pending());
  TryCreateNextConnection();
}

void LowEnergyConnectionManager::OnDisconnectionComplete(const hci::EventPacket& event) {
  FXL_DCHECK(event.event_code() == hci::kDisconnectionCompleteEventCode);
  const auto& params = event.view().payload<hci::DisconnectionCompleteEventParams>();
  hci::ConnectionHandle handle = le16toh(params.connection_handle);

  if (params.status != hci::Status::kSuccess) {
    FXL_LOG(WARNING) << fxl::StringPrintf(
        "gap: LowEnergyConnectionManager: HCI disconnection event received with error "
        "status: 0x%02x, handle: 0x%04x",
        params.status, handle);
    return;
  }

  FXL_LOG(INFO) << fxl::StringPrintf(
      "gap: LowEnergyConnectionManager: Link disconnected - "
      "status: 0x%02x, handle: 0x%04x, reason: 0x%02x",
      params.status, handle, params.reason);

  if (test_disconn_cb_) test_disconn_cb_(handle);

  // See if we can find a connection with a matching handle by walking the connections list.
  for (auto iter = connections_.begin(); iter != connections_.end(); ++iter) {
    FXL_DCHECK(iter->second.conn);
    if (iter->second.conn->handle() != handle) continue;

    // Found the connection. At this point it is OK to invalidate |iter| as the loop will terminate.
    auto conn_state = std::move(iter->second);
    connections_.erase(iter);

    FXL_DCHECK(!conn_state.refs.empty());

    // Mark the connection as closed so that hci::Connection::Close() becomes a NOP.
    conn_state.conn->set_closed();
    CleanUpConnectionState(&conn_state);

    return;
  }

  FXL_VLOG(1) << fxl::StringPrintf(
      "gap: LowEnergyConnectionManager: unknown connection handle: 0x%04x", handle);
}

}  // namespace gap
}  // namespace bluetooth
