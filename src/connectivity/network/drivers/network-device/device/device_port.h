// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_DEVICE_PORT_H_
#define SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_DEVICE_PORT_H_

#include <fuchsia/hardware/network/device/cpp/banjo.h>
#include <fuchsia/hardware/network/mac/cpp/banjo.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/stdcompat/optional.h>

#include <fbl/mutex.h>

#include "src/connectivity/network/drivers/network-device/mac/public/network_mac.h"
#include "status_watcher.h"

namespace network::internal {

class DevicePort : public fidl::WireServer<netdev::Port> {
 public:
  using TeardownCallback = fit::callback<void(DevicePort&)>;
  DevicePort(async_dispatcher_t* dispatcher, uint8_t id, ddk::NetworkPortProtocolClient port,
             std::unique_ptr<MacAddrDeviceInterface> mac, TeardownCallback on_teardown);
  ~DevicePort() { port_.Removed(); }

  uint8_t id() const { return port_id_; }
  ddk::NetworkPortProtocolClient& impl() { return port_; }

  // Notifies port of status changes notifications from the network device implementation.
  void StatusChanged(const port_status_t& new_status);
  // Starts port teardown process.
  //
  // Once port is torn down and ready to be deleted, the teardown callback passed on construction
  // will be called.
  // Calling teardown while a teardown is already in progress is a no-op.
  void Teardown();
  // Notifies the port a session attached to it.
  //
  // When sessions attach to a port, the port will notify the network port implementation that the
  // port is active.
  void SessionAttached();
  // Notifies the port a session detached from it.
  //
  // When all sessions are detached from a port, the port will notify the network port
  // implementation that the port is inactive.
  void SessionDetached();

  // Binds a new FIDL request to this port.
  void Bind(fidl::ServerEnd<netdev::Port> req);

  // Returns true if `frame_type` is a valid inbound frame type for subscription to on this port.
  bool IsValidRxFrameType(netdev::wire::FrameType frame_type) const;
  // Returns true if `frame_type` is a valid outbound frame type for this port.
  bool IsValidTxFrameType(netdev::wire::FrameType frame_type) const;

  // FIDL protocol implementation.
  void GetInfo(GetInfoRequestView request, GetInfoCompleter::Sync& completer) override;
  void GetStatus(GetStatusRequestView request, GetStatusCompleter::Sync& completer) override;
  void GetStatusWatcher(GetStatusWatcherRequestView request,
                        GetStatusWatcherCompleter::Sync& _completer) override;
  void GetMac(GetMacRequestView request, GetMacCompleter::Sync& _completer) override;

 private:
  // Helper class to keep track of clients bound to DevicePort.
  class Binding : public fbl::DoublyLinkedListable<std::unique_ptr<Binding>> {
   public:
    Binding() = default;
    void Unbind() {
      if (binding_.has_value()) {
        binding_->Unbind();
      }
    }
    void Bind(fidl::ServerBindingRef<netdev::Port> binding) { binding_ = std::move(binding); }

   private:
    std::optional<fidl::ServerBindingRef<netdev::Port>> binding_;
  };

  using BindingList = fbl::DoublyLinkedList<std::unique_ptr<Binding>>;

  // Concludes an ongoing teardown if it is ongoing and all internal resources are released.
  //
  // Returns true if teardown callback was dispatched.
  // Callers must assume the port is destroyed immediately if this function returns true.
  bool MaybeFinishTeardown() __TA_REQUIRES(lock_);
  // Implements session attachment and detachment.
  //
  // Notifies network port implementation that the port is active when `new_count` is 1.
  // Notifies network port implementation that the port is inactive when `new_count` is 0.
  // No-op otherwise.
  void NotifySessionCount(size_t new_count) __TA_EXCLUDES(lock_);

  async_dispatcher_t* const dispatcher_;
  const uint8_t port_id_;
  ddk::NetworkPortProtocolClient port_;
  std::unique_ptr<MacAddrDeviceInterface> mac_ __TA_GUARDED(lock_);
  BindingList bindings_ __TA_GUARDED(lock_);

  netdev::wire::DeviceClass port_class_;
  std::array<netdev::wire::FrameType, netdev::wire::kMaxFrameTypes> supported_rx_;
  size_t supported_rx_count_;
  std::array<netdev::wire::FrameTypeSupport, netdev::wire::kMaxFrameTypes> supported_tx_;
  size_t supported_tx_count_;

  fbl::Mutex lock_;
  StatusWatcherList watchers_ __TA_GUARDED(lock_);
  TeardownCallback on_teardown_ __TA_GUARDED(lock_);
  bool teardown_started_ __TA_GUARDED(lock_) = false;
  size_t attached_sessions_count_ __TA_GUARDED(lock_) = 0;

  DISALLOW_COPY_ASSIGN_AND_MOVE(DevicePort);
};

}  // namespace network::internal

#endif  // SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_DEVICE_PORT_H_
