// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <queue>
#include <unordered_set>

#include <fbl/function.h>

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/tasks/task_runner.h"

#include "garnet/drivers/bluetooth/lib/gap/remote_device.h"
#include "garnet/drivers/bluetooth/lib/hci/command_channel.h"
#include "garnet/drivers/bluetooth/lib/hci/control_packets.h"

namespace btlib {

namespace hci {
class Transport;
}  // namespace hci

namespace gap {

class BrEdrDiscoveryManager;
class RemoteDeviceCache;

// BrEdrDiscoveryManager implements discovery for BR/EDR devices.  We provide a
// mechanism for multiple clients to simultaneously request discovery.  Devices
// discovered will be added to the RemoteDeviceCache.
//
// Only one instance of BrEdrDiscoveryManager should be created for a bt-host.
//
// Request discovery using RequestDiscovery() which will provide a
// BrEdrDiscoverySession object in the |callback| when discovery is started.
// Ownership of this session is passed to the caller; when no sessions exist,
// discovery is halted.
//
// TODO(jamuraa): Name resolution should also happen here. (NET-509)
//
// This class is not thread-safe, BrEdrDiscoverySessions should be created and
// accessed on the same thread the BrEdrDiscoveryManager is created.
class BrEdrDiscoverySession final {
 public:
  // Destroying a session instance ends this discovery session. Discovery may
  // continue if other clients have started discovery sesisons.
  ~BrEdrDiscoverySession();

  // Set a result callback that will be notified whenever a result is returned
  // from the controller.  You will get duplicate results when using this
  // method.
  // Prefer RemoteDeviceCache.OnleviceUpdateCallback() instead.
  using DeviceFoundCallback = fbl::Function<void(const RemoteDevice& device)>;
  void set_result_callback(DeviceFoundCallback callback) {
    device_found_callback_ = std::move(callback);
  }

  // Set a callback to be notified if the session becomes inactive because
  // of internal errors.
  void set_error_callback(fbl::Closure callback) {
    error_callback_ = std::move(callback);
  }

 private:
  friend class BrEdrDiscoveryManager;

  // Used by the BrEdrDiscoveryManager to create a session.
  explicit BrEdrDiscoverySession(fxl::WeakPtr<BrEdrDiscoveryManager> manager);

  // Called by the BrEdrDiscoveryManager when a device report is found.
  void NotifyDiscoveryResult(const RemoteDevice& device) const;

  // Marks this session as ended because of an error.
  void NotifyError() const;

  fxl::WeakPtr<BrEdrDiscoveryManager> manager_;
  fbl::Closure error_callback_;
  DeviceFoundCallback device_found_callback_;
  fxl::ThreadChecker thread_checker_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BrEdrDiscoverySession);
};

class BrEdrDiscoveryManager final {
 public:
  // |device_cache| MUST out-live this BrEdrDiscoveryManager.
  BrEdrDiscoveryManager(fxl::RefPtr<hci::Transport> hci,
                        RemoteDeviceCache* device_cache);

  ~BrEdrDiscoveryManager();

  // Starts discovery and reports the status via |callback|. If discovery has
  // been successfully started, the callback will receive a session object that
  // it owns. If no sessions are owned, device discovery is stopped.
  using SessionCallback =
      std::function<void(hci::Status status,
                         std::unique_ptr<BrEdrDiscoverySession> session)>;
  void RequestDiscovery(const SessionCallback& callback);

  // Returns whether a discovery session is active.
  bool discovering() const { return !sessions_.empty(); }

 private:
  friend class BrEdrDiscoverySession;

  // Starts the inquiry procedure if any sessions exist.
  void MaybeStartInquiry();

  // Stops the inquiry procedure.
  void StopInquiry();

  // Used to receive Inquiry Results.
  void InquiryResult(const hci::EventPacket& event);

  // Creates and stores a new session object and returns it.
  std::unique_ptr<BrEdrDiscoverySession> AddSession();

  // Removes |session_| from the active sessions.
  void RemoveSession(BrEdrDiscoverySession* session);

  // Invalidates all current sessions, invoking their error callbacks.
  void InvalidateSessions();

  // The HCI Transport
  fxl::RefPtr<hci::Transport> hci_;

  // The task runner that we use for invoking callbacks asynchronously.
  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  // Device cache to use.
  // We hold a raw pointer is because it must out-live us.
  RemoteDeviceCache* cache_;

  // The list of currently active sessions. We store raw pointers here as we
  // don't own the sessions.  Sessions notify us when they are destroyed so we
  // can maintain this list.
  //
  // When |sessions_| becomes empty then scanning is stopped.
  std::unordered_set<BrEdrDiscoverySession*> sessions_;

  // The Handler ID of the event handler if we are scanning.
  hci::CommandChannel::EventHandlerId result_handler_id_;

  // The set of callbacks that are waiting on inquiry to start.
  std::queue<SessionCallback> pending_;

  fxl::ThreadChecker thread_checker_;

  fxl::WeakPtrFactory<BrEdrDiscoveryManager> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BrEdrDiscoveryManager);
};

}  // namespace gap
}  // namespace btlib
