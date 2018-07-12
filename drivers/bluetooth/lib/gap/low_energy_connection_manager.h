// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <list>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include "garnet/drivers/bluetooth/lib/gap/gap.h"
#include "garnet/drivers/bluetooth/lib/gatt/gatt.h"
#include "garnet/drivers/bluetooth/lib/hci/command_channel.h"
#include "garnet/drivers/bluetooth/lib/hci/control_packets.h"
#include "garnet/drivers/bluetooth/lib/hci/low_energy_connector.h"
#include "garnet/drivers/bluetooth/lib/l2cap/l2cap.h"

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace btlib {

namespace hci {
class Transport;
}  // namespace hci

namespace gap {

namespace internal {
class LowEnergyConnection;
}  // namespace internal

// TODO(armansito): Document the usage pattern.

class LowEnergyConnectionManager;
class RemoteDevice;
class RemoteDeviceCache;

class LowEnergyConnectionRef final {
 public:
  // Destroying this object releases its reference to the underlying connection.
  ~LowEnergyConnectionRef();

  // Releases this object's reference to the underlying connection.
  void Release();

  // Returns true if the underlying connection is still active.
  bool active() const { return active_; }

  // Sets a callback to be called when the underlying connection is closed.
  void set_closed_callback(fit::closure callback) {
    closed_cb_ = std::move(callback);
  }

  const std::string& device_identifier() const { return device_id_; }
  hci::ConnectionHandle handle() const { return handle_; }

 private:
  friend class LowEnergyConnectionManager;
  friend class internal::LowEnergyConnection;

  LowEnergyConnectionRef(const std::string& device_id,
                         hci::ConnectionHandle handle,
                         fxl::WeakPtr<LowEnergyConnectionManager> manager);

  // Called by LowEnergyConnectionManager when the underlying connection is
  // closed. Notifies |closed_cb_|.
  void MarkClosed();

  bool active_;
  std::string device_id_;
  hci::ConnectionHandle handle_;
  fxl::WeakPtr<LowEnergyConnectionManager> manager_;
  fit::closure closed_cb_;
  fxl::ThreadChecker thread_checker_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LowEnergyConnectionRef);
};

using LowEnergyConnectionRefPtr = std::unique_ptr<LowEnergyConnectionRef>;

class LowEnergyConnectionManager final {
 public:
  LowEnergyConnectionManager(fxl::RefPtr<hci::Transport> hci,
                             hci::LowEnergyConnector* connector,
                             RemoteDeviceCache* device_cache,
                             fbl::RefPtr<l2cap::L2CAP> l2cap,
                             fbl::RefPtr<gatt::GATT> gatt);
  ~LowEnergyConnectionManager();

  // Allows a caller to claim shared ownership over a connection to the
  // requested remote LE device identified by |device_identifier|. Returns
  // false, if |device_identifier| is not recognized, otherwise:
  //
  //   * If the requested device is already connected, this method
  //     asynchronously returns a LowEnergyConnectionRef without sending any
  //     requests to the controller. This is done for both local and remote
  //     initiated connections (i.e. the local adapter can either be in the LE
  //     central or peripheral roles). |callback| always succeeds.
  //
  //   * If the requested device is NOT connected, then this method initiates a
  //     connection to the requested device using one of the GAP central role
  //     connection establishment procedures described in Core Spec v5.0, Vol 3,
  //     Part C, Section 9.3. A LowEnergyConnectionRef is asynchronously
  //     returned to the caller once the connection has been set up.
  //
  //     The status of the procedure is reported in |callback| in the case of an
  //     error.
  //
  // |callback| is posted on the creation thread's dispatcher.
  using ConnectionResultCallback =
      fit::function<void(hci::Status, LowEnergyConnectionRefPtr)>;
  bool Connect(const std::string& device_identifier,
               ConnectionResultCallback callback);

  // Disconnects any existing LE connection to |device_identifier|, invalidating
  // all active LowEnergyConnectionRefs. Returns false if |device_identifier| is
  // not recognized or the corresponding remote device is not connected.
  bool Disconnect(const std::string& device_identifier);

  // Initializes a new connection over the given |link| and returns a connection
  // reference. Returns nullptr if the connection was rejected.
  //
  // |link| must be the result of a remote initiated connection.
  //
  // TODO(armansito): Add an |own_address| parameter for the locally advertised
  // address that was connected to.
  //
  // A link with the given handle should not have been previously registered.
  LowEnergyConnectionRefPtr RegisterRemoteInitiatedLink(
      hci::ConnectionPtr link);

  // TODO(armansito): Add a RemoteDeviceCache::Observer interface and move these
  // callbacks there.

  // Called when the connection parameters on a link have been updated.
  using ConnectionParametersCallback = fit::function<void(const RemoteDevice&)>;
  void SetConnectionParametersCallbackForTesting(
       ConnectionParametersCallback callback);

  // Called when a link with the given handle gets disconnected. This event is
  // guaranteed to be called before invalidating connection references.
  // |callback| is run on the creation thread.
  //
  // NOTE: This is intended ONLY for unit tests. Clients should watch for
  // disconnection events using LowEnergyConnectionRef::set_closed_callback()
  // instead. DO NOT use outside of tests.
  using DisconnectCallback = fit::function<void(hci::ConnectionHandle)>;
  void SetDisconnectCallbackForTesting(DisconnectCallback callback);

  // Sets the timeout interval to be used on future connect requests. The
  // default value is kLECreateConnecionTimeoutMs.
  void set_request_timeout_for_testing(int64_t value_ms) {
    request_timeout_ms_ = value_ms;
  }

 private:
  friend class LowEnergyConnectionRef;

  // Mapping from device identifiers to open LE connections.
  using ConnectionMap =
      std::unordered_map<std::string,
                         std::unique_ptr<internal::LowEnergyConnection>>;

  class PendingRequestData {
   public:
    PendingRequestData(const common::DeviceAddress& address,
                       ConnectionResultCallback first_callback);
    PendingRequestData() = default;
    ~PendingRequestData() = default;

    PendingRequestData(PendingRequestData&&) = default;
    PendingRequestData& operator=(PendingRequestData&&) = default;

    void AddCallback(ConnectionResultCallback cb) {
      callbacks_.push_back(std::move(cb));
    }

    // Notifies all elements in |callbacks| with |status| and the result of
    // |func|.
    using RefFunc = fit::function<LowEnergyConnectionRefPtr()>;
    void NotifyCallbacks(hci::Status status, const RefFunc& func);

    const common::DeviceAddress& address() const { return address_; }

   private:
    common::DeviceAddress address_;
    std::list<ConnectionResultCallback> callbacks_;

    FXL_DISALLOW_COPY_AND_ASSIGN(PendingRequestData);
  };

  // Called by LowEnergyConnectionRef::Release().
  void ReleaseReference(LowEnergyConnectionRef* conn_ref);

  // Called when |connector_| completes a pending request. Initiates a new
  // connection attempt for the next device in the pending list, if any.
  void TryCreateNextConnection();

  // Initiates a connection attempt to |peer|.
  void RequestCreateConnection(RemoteDevice* peer);

  // Initializes the connection to the peer with the given identifier and
  // returns the initial reference to it. This method is responsible for setting
  // up all data bearers.
  LowEnergyConnectionRefPtr InitializeConnection(const std::string& device_id,
                                                 hci::ConnectionPtr link);

  // Adds a new connection reference to an existing connection to the device
  // with the ID |device_identifier| and returns it. Returns nullptr if
  // |device_identifier| is not recognized.
  LowEnergyConnectionRefPtr AddConnectionRef(
      const std::string& device_identifier);

  // Cleans up a connection state. This results in a HCI_Disconnect command
  // if |close_link| is true, and notifies any referenced
  // LowEnergyConnectionRefs of the disconnection. Marks the corresponding
  // RemoteDeviceCache entry as disconnected and cleans up all data bearers.
  //
  // |conn_state| will have been removed from the underlying map at the time of
  // a call. Its ownership is passed to the method for disposal.
  //
  // This is also responsible for unregistering the link from managed subsystems
  // (e.g. L2CAP).
  void CleanUpConnection(std::unique_ptr<internal::LowEnergyConnection> conn,
                         bool close_link = true);

  // Called by |connector_| when a new locally initiated LE connection has been
  // created.
  void RegisterLocalInitiatedLink(hci::ConnectionPtr link);

  // Updates |device_cache_| with the given |link| and returns the corresponding
  // RemoteDevice.
  //
  // Creates a new RemoteDevice if |link| matches a peer that did not
  // previously exist in the cache. Otherwise this updates and returns an
  // existing RemoteDevice.
  //
  // The returned device is marked as non-temporary and its connection
  // parameters are updated.
  //
  // Called by RegisterRemoteInitiatedLink() and RegisterLocalInitiatedLink().
  RemoteDevice* UpdateRemoteDeviceWithLink(const hci::Connection& link);

  // Called by |connector_| to indicate the result of a connect request.
  void OnConnectResult(const std::string& device_identifier,
                       hci::Status status,
                       hci::ConnectionPtr link);

  // Event handler for the HCI Disconnection Complete event.
  // TODO(armansito): This needs to be shared between the BR/EDR and LE
  // connection managers, so this handler should be moved elsewhere.
  void OnDisconnectionComplete(const hci::EventPacket& event);

  // Event handler for the HCI LE Connection Update Complete event.
  void OnLEConnectionUpdateComplete(const hci::EventPacket& event);

  // Called when the preferred connection parameters have been received for a LE
  // peripheral. This can happen in the form of:
  //
  //   1. <<Slave Connection Interval Range>> advertising data field
  //   2. "Peripheral Preferred Connection Parameters" GATT characteristic
  //      (under "GAP" service)
  //   3. HCI LE Remote Connection Parameter Request Event
  //   4. L2CAP Connection Parameter Update request
  //
  // TODO(armansito): Support #1, #2, and #3 above.
  //
  // This method caches |params| for later connection attempts and sends the
  // parameters to the controller if the initializing procedures are complete
  // (since we use more agressing initial parameters for pairing and service
  // discovery, as recommended by the specification in v5.0, Vol 3, Part C,
  // Section 9.3.12.1).
  //
  // |device_identifier| uniquely identifies the peer. |handle| represents the
  // the logical link that |params| should be applied to.
  void OnNewLEConnectionParams(
      const std::string& device_identifier,
      hci::ConnectionHandle handle,
      const hci::LEPreferredConnectionParameters& params);

  // Tells the controller to use the given connection |params| on the given
  // logical link |handle|.
  void UpdateConnectionParams(
      hci::ConnectionHandle handle,
      const hci::LEPreferredConnectionParameters& params);

  // Returns an iterator into |connections_| if a connection is found that
  // matches the given logical link |handle|. Otherwise, returns an iterator
  // that is equal to |connections_.end()|.
  //
  // The general rules of validity around std::unordered_map::iterator apply to
  // the returned value.
  ConnectionMap::iterator FindConnection(hci::ConnectionHandle handle);

  fxl::RefPtr<hci::Transport> hci_;

  // Time after which a connection attempt is considered to have timed out. This
  // is configurable to allow unit tests to set a shorter value.
  int64_t request_timeout_ms_;

  // The dispather for all asynchronous tasks.
  async_dispatcher_t* dispatcher_;

  // The device cache is used to look up and persist remote device data that is
  // relevant during connection establishment (such as the address, preferred
  // connetion parameters, etc). Expected to outlive this instance.
  RemoteDeviceCache* device_cache_;  // weak

  // The L2CAP layer reference, used to manage LE logical links and fixed
  // channels. LE-specific L2CAP signaling events (e.g. connection parameter
  // update) are received here.
  fbl::RefPtr<l2cap::L2CAP> l2cap_;

  // The GATT layer reference, used to add and remove ATT data bearers and
  // service discovery.
  fbl::RefPtr<gatt::GATT> gatt_;

  // Local GATT service registry.
  std::unique_ptr<gatt::LocalServiceManager> gatt_registry_;

  // Event handler ID for the HCI Disconnection Complete event.
  hci::CommandChannel::EventHandlerId disconn_cmpl_handler_id_;

  // Event handler ID for the HCI LE Connection Update Complete event.
  hci::CommandChannel::EventHandlerId conn_update_cmpl_handler_id_;

  // Callbacks used by unit tests to observe connection state events.
  ConnectionParametersCallback test_conn_params_cb_;
  DisconnectCallback test_disconn_cb_;

  // Outstanding connection requests based on remote device ID.
  std::unordered_map<std::string, PendingRequestData> pending_requests_;

  // Mapping from device identifiers to currently open LE connections.
  ConnectionMap connections_;

  // Performs the Direct Connection Establishment procedure. |connector_| must
  // out-live this connection manager.
  hci::LowEnergyConnector* connector_;  // weak

  // Keep this as the last member to make sure that all weak pointers are
  // invalidated before other members get destroyed.
  fxl::WeakPtrFactory<LowEnergyConnectionManager> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LowEnergyConnectionManager);
};

}  // namespace gap
}  // namespace btlib
