// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_DEVICE_INTERFACE_H_
#define SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_DEVICE_INTERFACE_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include <ddktl/protocol/network/device.h>

#include "data_structs.h"
#include "definitions.h"
#include "public/network_device.h"
#include "rx_queue.h"
#include "session.h"
#include "status_watcher.h"
#include "tx_queue.h"

namespace network::internal {

enum class DeviceStatus { STARTING, STARTED, STOPPING, STOPPED };

enum class PendingDeviceOperation { NONE, START, STOP };

class DeviceInterface : public netdev::Device::Interface,
                        public ddk::NetworkDeviceIfcProtocol<DeviceInterface>,
                        public ::network::NetworkDeviceInterface {
 public:
  static zx_status_t Create(async_dispatcher_t* dispatcher,
                            ddk::NetworkDeviceImplProtocolClient parent, const char* parent_name,
                            std::unique_ptr<DeviceInterface>* out);
  ~DeviceInterface() override;

  // Public NetworkDevice API.
  void Teardown(fit::callback<void()> callback) override;
  zx_status_t Bind(zx::channel req) override;

  // NetworkDevice interface implementation.
  void NetworkDeviceIfcStatusChanged(const status_t* new_status);
  void NetworkDeviceIfcCompleteRx(const rx_buffer_t* rx_list, size_t rx_count);
  void NetworkDeviceIfcCompleteTx(const tx_result_t* tx_list, size_t tx_count);
  void NetworkDeviceIfcSnoop(const rx_buffer_t* rx_list, size_t rx_count);

  uint32_t rx_fifo_depth() const;
  uint32_t tx_fifo_depth() const;
  // Returns the device-onwed buffers count threshold at which we should trigger RxQueue work. If
  // the number of buffers on device is less than or equal to the threshold, we should attempt to
  // fetch more buffers.
  uint32_t rx_notify_threshold() const {
    // TODO(fxbug.dev/44835): This threshold should be negotiated as part of the device info in the
    // banjo protocol.
    return device_info_.rx_depth / 2;
  }

  TxQueue& tx_queue() { return *tx_queue_; }

  const device_info_t& info() { return device_info_; }

  // Loads rx path descriptors from the primary session into a session transaction.
  zx_status_t LoadRxDescriptors(RxQueue::SessionTransaction* transact);
  bool IsValidRxFrameType(uint8_t frame_type) const;
  bool IsValidTxFrameType(uint8_t frame_type) const;

  // Operates workflow for when a session is started. If the session is eligible to take over the
  // primary spot, it'll be elected the new primary session. If there was no primary session before,
  // the data path will be started BEFORE the new session is elected as primary,
  void SessionStarted(Session* session);
  // Operates workflow for when a session is stopped. If there's another session that is eligible to
  // take over the primary spot, it'll be elected the new primary session. Otherwise, the data path
  // will be stopped.
  void SessionStopped(Session* session);

  // If a primary session exists, primary_rx_fifo returns a reference-counted pointer to the primary
  // session's Rx FIFO. Otherwise, the returned pointer is null.
  fbl::RefPtr<RefCountedFifo> primary_rx_fifo();

  // Commits all pending rx buffers in all active sessions.
  void CommitAllSessions();
  // Copies the received data described by `buff` to all sessions other than `owner`.
  void CopySessionData(const Session& owner, uint16_t owner_index, const rx_buffer_t* buff);
  // Notifies all listening sessions of a new tx transaction from session `owner` and descriptor
  // `owner_index`. Returns true if any listening sessions copied the data.
  bool ListenSessionData(const Session& owner, uint16_t owner_index);

  // Notifies all sessions that the transmit queue has available spots to take in transmit frames.
  void NotifyTxQueueAvailable();
  // Sends the provided space buffers in `rx` to the device implementation.
  void QueueRxSpace(const rx_space_buffer_t* rx, size_t count);
  // Sends the provided transmit buffers in `rx` to the device implementation.
  void QueueTx(const tx_buffer_t* tx, size_t count);
  bool IsDataPlaneOpen();

  // Called by sessions when they're no longer running. If the dead session has any outstanding
  // buffers with the device implementation, it'll be kept in `dead_sessions_` until all the buffers
  // are safely returned and we own all the buffers again.
  void NotifyDeadSession(Session* dead_session);
  // Destroys all dead sessions that report they can be destroyed through `Session::CanDestroy`.
  void PruneDeadSessions();

  // Registers `vmo` as a data vmo that will be shared with the device implementation. On success,
  // `out_id` contains the generated identifier and `out_stored_vmo` contains an unowned pointer to
  // the `StoredVmo` holder.
  zx_status_t RegisterDataVmo(zx::vmo vmo, uint8_t* out_id,
                              DataVmoStore::StoredVmo** out_stored_vmo);

  // Fidl protocol implementation.
  void GetInfo(GetInfoCompleter::Sync& completer) override;
  void GetStatus(GetStatusCompleter::Sync& completer) override;
  void OpenSession(::fidl::StringView session_name, netdev::SessionInfo session_info,
                   OpenSessionCompleter::Sync& completer) override;
  void GetStatusWatcher(zx::channel watcher, uint32_t buffer,
                        GetStatusWatcherCompleter::Sync& completer) override;

  // Serves the OpenSession FIDL handle method synchronously.
  zx_status_t OpenSession(fidl::StringView name, netdev::SessionInfo session_info,
                          netdev::Device_OpenSession_Response* rsp);

 private:
  // Helper class to keep track of clients bound to DeviceInterface.
  class Binding : public fbl::DoublyLinkedListable<std::unique_ptr<Binding>> {
   public:
    static zx_status_t Bind(DeviceInterface* interface, zx::channel channel);
    void Unbind();

   private:
    Binding() = default;
    fit::optional<fidl::ServerBindingRef<netdev::Device>> binding_;
  };
  using BindingList = fbl::DoublyLinkedList<std::unique_ptr<Binding>>;

  enum class TeardownState { RUNNING, BINDINGS, WATCHERS, SESSIONS, FINISHED };

  explicit DeviceInterface(async_dispatcher_t* dispatcher,
                           ddk::NetworkDeviceImplProtocolClient parent)
      : dispatcher_(dispatcher),
        device_(parent),
        vmo_store_(vmo_store::Options{
            vmo_store::MapOptions{ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_REQUIRE_NON_RESIZABLE,
                                  nullptr},
            fit::nullopt}) {}
  zx_status_t Init(const char* parent_name);

  // Starts the data path with the device implementation.
  void StartDevice();
  // Stops the data path with the device implementation.
  void StopDevice();
  // Starts the device implementation with `DeviceStarted` as its callback.
  void StartDeviceInner();
  // Stops the device implementation with `DeviceStopped` as its callback.
  void StopDeviceInner();

  // Callback given to the device implementation for the `Start` call. The data path is considered
  // open only once the device is started.
  void DeviceStarted();
  // Callback given to the device implementation for the `Stop` call. All outstanding buffers are
  // automatically reclaimed once the device is considered stopped. If a teardown is pending,
  // `DeviceStopped` will complete the teardown BEFORE all buffers are reclaimed and all the
  // sessions are destroyed.
  void DeviceStopped();

  PendingDeviceOperation SetDeviceStatus(DeviceStatus status);

  // Notifies the device implementation that the VMO used by the provided session will no longer be
  // used. It is called right before sessions are destroyed.
  // ReleaseVMO acquires the vmos_lock_ internally, so we mark it as excluding the vmos_lock_.
  void ReleaseVmo(Session* session) __TA_EXCLUDES(vmos_lock_);

  // Continues a teardown process, if one is running.
  //
  // The provided state is the expected state that the teardown process is in. If the given state is
  // not the current teardown state, no processing will happen. Otherwise, the teardown process will
  // continue if the pre-conditions to move between teardown states are met.
  //
  // Returns true if the teardown is completed and execution should be stopped.
  // ContinueTeardown is marked with many thread analysis lock exclusions so it can acquire those
  // locks internally and evaluate the teardown progress.
  bool ContinueTeardown(TeardownState state) __TA_RELEASE(teardown_lock_)
      __TA_EXCLUDES(sessions_lock_) __TA_EXCLUDES(dead_sessions_lock_) __TA_EXCLUDES(bindings_lock_)
          __TA_EXCLUDES(watchers_lock_);

  // Immutable information BEFORE initialization:
  device_info_t device_info_{};
  // dispatcher used for slow-path operations:
  async_dispatcher_t* const dispatcher_;
  const ddk::NetworkDeviceImplProtocolClient device_;
  std::array<uint8_t, netdev::MAX_FRAME_TYPES> supported_rx_{};
  std::array<tx_support_t, netdev::MAX_FRAME_TYPES> supported_tx_{};
  std::array<uint8_t, netdev::MAX_ACCEL_FLAGS> accel_rx_{};
  std::array<uint8_t, netdev::MAX_ACCEL_FLAGS> accel_tx_{};

  fbl::Mutex sessions_lock_ __TA_ACQUIRED_BEFORE(dead_sessions_lock_)
      __TA_ACQUIRED_BEFORE(vmos_lock_);
  std::unique_ptr<Session> primary_session_ __TA_GUARDED(sessions_lock_);
  SessionList sessions_ __TA_GUARDED(sessions_lock_);
  uint32_t active_primary_sessions_ __TA_GUARDED(sessions_lock_) = 0;

  fbl::Mutex watchers_lock_;
  StatusWatcherList watchers_ __TA_GUARDED(watchers_lock_);

  fbl::Mutex dead_sessions_lock_ __TA_ACQUIRED_BEFORE(vmos_lock_);
  SessionList dead_sessions_ __TA_GUARDED(dead_sessions_lock_);

  fbl::Mutex vmos_lock_;
  // We don't need to keep any data associated with the VMO ids, we use the slab to guarantee
  // non-overlapping unique identifiers within a set of valid IDs.
  DataVmoStore vmo_store_ __TA_GUARDED(vmos_lock_);
  std::unique_ptr<IndexedSlab<nullptr_t>> vmo_ids_ __TA_GUARDED(vmos_lock_);

  fbl::Mutex bindings_lock_;
  BindingList bindings_ __TA_GUARDED(bindings_lock_);

  fbl::Mutex teardown_lock_ __TA_ACQUIRED_BEFORE(sessions_lock_)
      __TA_ACQUIRED_BEFORE(dead_sessions_lock_) __TA_ACQUIRED_BEFORE(bindings_lock_)
          __TA_ACQUIRED_BEFORE(watchers_lock_);
  TeardownState teardown_state_ = TeardownState::RUNNING;
  fit::callback<void()> teardown_callback_ __TA_GUARDED(teardown_lock_);

  PendingDeviceOperation pending_device_op_ = PendingDeviceOperation::NONE;
  std::atomic_bool has_listen_sessions_ = false;

  // NOTE: when locking the queues explicitly, the tx_queue_->Lock should ALWAYS be acquired before
  // rx_queue_->Lock
  std::unique_ptr<TxQueue> tx_queue_;
  std::unique_ptr<RxQueue> rx_queue_;

  // NOTE: device_status_ should only be written to with both the tx_queue_ and rx_queue_ locks
  // acquired. The internal implementation of each queue will read the device status while holding
  // each individual lock.
  DeviceStatus device_status_ = DeviceStatus::STOPPED;

 public:
  // Event hooks used in tests:
  fit::function<void(const char*)> evt_session_started;
  // Unsafe accessors used in tests:
  const SessionList& sessions_unsafe() const { return sessions_; }
};

}  // namespace network::internal

#endif  // SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_DEVICE_INTERFACE_H_
