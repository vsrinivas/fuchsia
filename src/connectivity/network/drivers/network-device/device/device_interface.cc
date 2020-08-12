// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_interface.h"

#include <zircon/device/network.h>

#include <fbl/alloc_checker.h>

#include "log.h"

namespace network {

zx_status_t NetworkDeviceInterface::Create(async_dispatcher_t* dispatcher,
                                           ddk::NetworkDeviceImplProtocolClient parent,
                                           const char* parent_name,
                                           std::unique_ptr<NetworkDeviceInterface>* out) {
  std::unique_ptr<internal::DeviceInterface> interface;
  zx_status_t status =
      internal::DeviceInterface::Create(dispatcher, parent, parent_name, &interface);
  *out = std::move(interface);
  return status;
}

namespace internal {

zx_status_t DeviceInterface::Create(async_dispatcher_t* dispatcher,
                                    ddk::NetworkDeviceImplProtocolClient parent,
                                    const char* parent_name,
                                    std::unique_ptr<DeviceInterface>* out) {
  fbl::AllocChecker ac;
  std::unique_ptr<DeviceInterface> device(new (&ac) DeviceInterface(dispatcher, parent));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  zx_status_t status = device->Init(parent_name);
  if (status == ZX_OK) {
    *out = std::move(device);
  }
  return status;
}

DeviceInterface::~DeviceInterface() {
  ZX_ASSERT_MSG(primary_session_ == nullptr,
                "Can't destroy DeviceInterface with active primary session. (%s)",
                primary_session_->name());
  ZX_ASSERT_MSG(sessions_.is_empty(), "Can't destroy DeviceInterface with %ld pending session(s).",
                sessions_.size_slow());
  ZX_ASSERT_MSG(dead_sessions_.is_empty(),
                "Can't destroy DeviceInterface with %ld pending dead session(s).",
                dead_sessions_.size_slow());
  ZX_ASSERT_MSG(bindings_.is_empty(), "Can't destroy device interface with %ld attached bindings.",
                bindings_.size_slow());
  ZX_ASSERT_MSG(watchers_.is_empty(), "Can't destroy device interface with %ld attached watchers.",
                watchers_.size_slow());
}

zx_status_t DeviceInterface::Init(const char* parent_name) {
  LOG_TRACE("network-device: Init");
  if (!device_.is_valid()) {
    LOG_ERROR("network-device: bind: no protocol");
    return ZX_ERR_INTERNAL;
  }

  network_device_impl_protocol_t proto;
  device_.GetProto(&proto);
  network_device_impl_protocol_ops_t* ops = proto.ops;
  if (ops->get_info == nullptr || ops->stop == nullptr || ops->start == nullptr ||
      ops->queue_tx == nullptr || ops->get_status == nullptr) {
    LOGF_ERROR("network-device: bind: device '%s': incomplete protocol", parent_name);
    return ZX_ERR_NOT_SUPPORTED;
  }

  device_.GetInfo(&device_info_);
  if (device_info_.rx_types_count > netdev::MAX_FRAME_TYPES ||
      device_info_.tx_types_count > netdev::MAX_FRAME_TYPES) {
    LOGF_ERROR("network-device: bind: device '%s' reports too many supported frame types",
               parent_name);
    return ZX_ERR_NOT_SUPPORTED;
  }
  // Copy the vectors of supported Rx and Tx types.
  std::copy_n(device_info_.rx_types_list, device_info_.rx_types_count, supported_rx_.begin());
  device_info_.rx_types_list = supported_rx_.data();
  std::copy_n(device_info_.tx_types_list, device_info_.tx_types_count, supported_tx_.begin());
  device_info_.tx_types_list = supported_tx_.data();

  if (device_info_.rx_accel_count > netdev::MAX_ACCEL_FLAGS ||
      device_info_.tx_accel_count > netdev::MAX_ACCEL_FLAGS) {
    LOGF_ERROR("network-device: bind: device '%s' reports too many acceleration flags",
               parent_name);
    return ZX_ERR_NOT_SUPPORTED;
  }
  // Copy the vectors of supported acceleration flags.
  std::copy_n(device_info_.rx_accel_list, device_info_.rx_accel_count, accel_rx_.begin());
  device_info_.rx_accel_list = accel_rx_.data();
  std::copy_n(device_info_.tx_accel_list, device_info_.tx_accel_count, accel_tx_.begin());
  device_info_.tx_accel_list = accel_tx_.data();

  if (device_info_.rx_depth > kMaxFifoDepth || device_info_.tx_depth > kMaxFifoDepth) {
    LOGF_ERROR("network-device: bind: device '%s' reports too large FIFO depths: %d/%d (max=%d)",
               parent_name, device_info_.rx_depth, device_info_.tx_depth, kMaxFifoDepth);
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t status;
  if ((status = TxQueue::Create(this, &tx_queue_)) != ZX_OK) {
    LOGF_ERROR("network-device: bind: device failed to start Tx Queue: %s",
               zx_status_get_string(status));
    return status;
  }

  if ((status = RxQueue::Create(this, &rx_queue_)) != ZX_OK) {
    LOGF_ERROR("network-device: bind: device failed to start Rx Queue: %s",
               zx_status_get_string(status));
    return status;
  }
  {
    fbl::AutoLock lock(&vmos_lock_);
    if ((status = vmo_store_.Reserve(MAX_VMOS)) != ZX_OK) {
      LOGF_ERROR("network-device: bind: failed to init session identifiers %s",
                 zx_status_get_string(status));
      return status;
    }
  }

  // Init session with parent.
  if ((status = device_.Init(this, &network_device_ifc_protocol_ops_)) != ZX_OK) {
    LOGF_ERROR("network-device: bind: NetworkDevice Init failed: %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

void DeviceInterface::Teardown(fit::callback<void()> teardown_callback) {
  // stop all rx queue operation immediately.
  rx_queue_->JoinThread();
  LOG_TRACE("network-device: Teardown");

  teardown_lock_.Acquire();
  // Can't call teardown again until the teardown process has ended.
  ZX_ASSERT(teardown_callback_ == nullptr);
  teardown_callback_ = std::move(teardown_callback);

  ContinueTeardown(TeardownState::RUNNING);
}

zx_status_t DeviceInterface::Bind(zx::channel req) {
  {
    fbl::AutoLock teardown_lock(&teardown_lock_);
    // Don't attach new bindings if we're tearing down.
    if (teardown_state_ != TeardownState::RUNNING) {
      return ZX_ERR_BAD_STATE;
    }
  }
  return Binding::Bind(this, std::move(req));
}

void DeviceInterface::NetworkDeviceIfcStatusChanged(const status_t* new_status) {
  LOGF_TRACE("network-device: StatusChanged %d %d", new_status->flags, new_status->mtu);
  fbl::AutoLock lock(&watchers_lock_);
  for (auto& w : watchers_) {
    w.PushStatus(*new_status);
  }
}

void DeviceInterface::NetworkDeviceIfcCompleteRx(const rx_buffer_t* rx_list, size_t rx_count) {
  rx_queue_->CompleteRxList(rx_list, rx_count);
}

void DeviceInterface::NetworkDeviceIfcCompleteTx(const tx_result_t* tx_list, size_t tx_count) {
  tx_queue_->CompleteTxList(tx_list, tx_count);
}

void DeviceInterface::NetworkDeviceIfcSnoop(const rx_buffer_t* rx_list, size_t rx_count) {
  // TODO(fxbug.dev/43028): Implement real version. Current implementation acts as if no LISTEN is
  // ever in place.
}

void DeviceInterface::GetInfo(GetInfoCompleter::Sync completer) {
  LOG_TRACE("network-device: GetInfo");
  netdev::Info info;
  info.class_ = static_cast<netdev::DeviceClass>(device_info_.device_class);
  info.min_descriptor_length = sizeof(buffer_descriptor_t) / sizeof(uint64_t);
  info.descriptor_version = NETWORK_DEVICE_DESCRIPTOR_VERSION;
  info.rx_depth = rx_fifo_depth();
  info.tx_depth = tx_fifo_depth();
  // TODO(fxbug.dev/44604): We're missing a way to negotiate the buffer alignment with the device
  // implementation. We're using a sufficiently large alignment for now for typical requirements we
  // see in drivers, but this needs to be fixed.
  info.buffer_alignment = ZX_PAGE_SIZE / 2;
  info.max_buffer_length = device_info_.max_buffer_length;
  info.min_rx_buffer_length = device_info_.min_rx_buffer_length;
  info.min_tx_buffer_head = device_info_.tx_head_length;
  info.min_tx_buffer_tail = device_info_.tx_tail_length;

  std::array<netdev::FrameType, netdev::MAX_FRAME_TYPES> rx;
  std::array<netdev::FrameTypeSupport, netdev::MAX_FRAME_TYPES> tx;

  for (size_t i = 0; i < device_info_.rx_types_count; i++) {
    rx[i] = static_cast<netdev::FrameType>(device_info_.rx_types_list[i]);
  }
  for (size_t i = 0; i < device_info_.tx_types_count; i++) {
    auto& dst = tx[i];
    auto& src = device_info_.tx_types_list[i];
    dst.features = src.features;
    dst.supported_flags = static_cast<netdev::TxFlags>(src.supported_flags);
    dst.type = static_cast<netdev::FrameType>(src.type);
  }

  info.rx_types.set_count(device_info_.rx_types_count);
  info.rx_types.set_data(fidl::unowned_ptr(rx.data()));
  info.tx_types.set_count(device_info_.tx_types_count);
  info.tx_types.set_data(fidl::unowned_ptr(tx.data()));

  std::array<netdev::RxAcceleration, netdev::MAX_ACCEL_FLAGS> rx_accel;
  std::array<netdev::TxAcceleration, netdev::MAX_ACCEL_FLAGS> tx_accel;
  for (size_t i = 0; i < device_info_.rx_accel_count; i++) {
    rx_accel[i] = static_cast<netdev::RxAcceleration>(device_info_.rx_accel_list[i]);
  }
  for (size_t i = 0; i < device_info_.tx_accel_count; i++) {
    tx_accel[i] = static_cast<netdev::TxAcceleration>(device_info_.tx_accel_list[i]);
  }
  info.rx_accel.set_count(device_info_.rx_accel_count);
  info.rx_accel.set_data(fidl::unowned_ptr(rx_accel.data()));
  info.tx_accel.set_count(device_info_.tx_accel_count);
  info.tx_accel.set_data(fidl::unowned_ptr(tx_accel.data()));

  completer.Reply(std::move(info));
}

void DeviceInterface::GetStatus(GetStatusCompleter::Sync completer) {
  status_t status;
  device_.GetStatus(&status);
  completer.Reply(FidlStatus(status).view());
}

void DeviceInterface::OpenSession(::fidl::StringView session_name, netdev::SessionInfo session_info,
                                  OpenSessionCompleter::Sync completer) {
  netdev::Device_OpenSession_Response rsp;
  zx_status_t status = OpenSession(std::move(session_name), std::move(session_info), &rsp);
  netdev::Device_OpenSession_Result result;
  if (status != ZX_OK) {
    result.set_err(fidl::unowned_ptr(&status));
  } else {
    result.set_response(fidl::unowned_ptr(&rsp));
  }
  completer.Reply(std::move(result));
}

void DeviceInterface::GetStatusWatcher(zx::channel watcher, uint32_t buffer,
                                       GetStatusWatcherCompleter::Sync _completer) {
  {
    fbl::AutoLock teardown_lock(&teardown_lock_);
    // We're currently tearing down and can't open any new watchers.
    if (teardown_state_ != TeardownState::RUNNING) {
      return;
    }
  }

  fbl::AllocChecker ac;
  auto n_watcher = fbl::make_unique_checked<StatusWatcher>(&ac, buffer);
  if (!ac.check()) {
    return;
  }
  zx_status_t status =
      n_watcher->Bind(dispatcher_, std::move(watcher), [this](StatusWatcher* watcher) {
        bool watchers_empty;
        teardown_lock_.Acquire();
        {
          fbl::AutoLock lock(&watchers_lock_);
          watchers_.erase(*watcher);
          watchers_empty = watchers_.is_empty();
        }
        if (watchers_empty) {
          ContinueTeardown(TeardownState::WATCHERS);
        } else {
          teardown_lock_.Release();
        }
      });
  if (status != ZX_OK) {
    LOGF_ERROR("network-device: Failed to bind watcher: %s", zx_status_get_string(status));
    return;
  }

  status_t device_status;
  fbl::AutoLock lock(&watchers_lock_);
  device_.GetStatus(&device_status);
  n_watcher->PushStatus(device_status);
  watchers_.push_back(std::move(n_watcher));
}

zx_status_t DeviceInterface::OpenSession(fidl::StringView name, netdev::SessionInfo session_info,
                                         netdev::Device_OpenSession_Response* rsp) {
  {
    fbl::AutoLock teardown_lock(&teardown_lock_);
    // We're currently tearing down and can't open any new sessions.
    if (teardown_state_ != TeardownState::RUNNING) {
      return ZX_ERR_UNAVAILABLE;
    }
  }
  fbl::AutoLock lock(&sessions_lock_);

  zx::channel req;
  zx_status_t status;
  if ((status = zx::channel::create(0, &req, &rsp->session)) != ZX_OK) {
    return status;
  }
  std::unique_ptr<Session> session;
  if ((status = Session::Create(dispatcher_, std::move(session_info), std::move(name), this,
                                std::move(req), &session, &rsp->fifos)) != ZX_OK) {
    return status;
  }

  if (session->ShouldTakeOverPrimary(primary_session_.get())) {
    // Set this new session as the primary session.
    std::swap(primary_session_, session);
    rx_queue_->TriggerSessionChanged();
  }
  if (session) {
    // Add the new session (or the primary session if it the new session just took over) to the list
    // of sessions.
    sessions_.push_back(std::move(session));
  }

  return ZX_OK;
}

uint32_t DeviceInterface::rx_fifo_depth() const {
  // We're going to say the depth is twice the depth of the device to account for in-flight
  // buffers, as long as it doesn't go over the maximum fifo depth.
  return std::min(kMaxFifoDepth, device_info_.rx_depth * 2);
}

uint32_t DeviceInterface::tx_fifo_depth() const {
  // We're going to say the depth is twice the depth of the device to account for in-flight
  // buffers, as long as it doesn't go over the maximum fifo depth;
  return std::min(kMaxFifoDepth, device_info_.tx_depth * 2);
}

void DeviceInterface::SessionStarted(Session* session) {
  bool should_start = false;
  {
    fbl::AutoLock lock(&sessions_lock_);
    if (session->IsListen()) {
      has_listen_sessions_.store(true, std::memory_order_relaxed);
    }
    if (session->IsPrimary()) {
      active_primary_sessions_++;
      if (session->ShouldTakeOverPrimary(primary_session_.get())) {
        // push primary session to sessions list.
        sessions_.push_back(std::move(primary_session_));
        // find the session in the list and promote it to primary
        primary_session_ = sessions_.erase(*session);
        ZX_ASSERT(primary_session_);
        // notify rx queue of primary session change
        rx_queue_->TriggerSessionChanged();
      }
      should_start = active_primary_sessions_ != 0;
    }
  }

  if (should_start) {
    // start the device if we haven't done so already.
    StartDevice();
  }

  if (evt_session_started) {
    evt_session_started(session->name());
  }
}

void DeviceInterface::SessionStopped(Session* session) {
  bool should_stop = false;
  {
    fbl::AutoLock lock(&sessions_lock_);
    if (session->IsListen()) {
      bool any = primary_session_ && primary_session_->IsListen() && !primary_session_->IsPaused();
      for (auto& s : sessions_) {
        any |= s.IsListen() && !s.IsPaused();
      }
      has_listen_sessions_.store(any, std::memory_order_relaxed);
    }

    if (session->IsPrimary()) {
      ZX_ASSERT(active_primary_sessions_ > 0);
      if (session == primary_session_.get()) {
        // if this was the primary session, offer all other sessions to take over:
        for (auto& i : sessions_) {
          if (i.ShouldTakeOverPrimary(session)) {
            session = &i;
          }
        }
        // if we found a candidate to take over primary...
        if (session != primary_session_.get()) {
          // ...promote it.
          sessions_.push_back(std::move(primary_session_));
          primary_session_ = sessions_.erase(*session);
          ZX_ASSERT(primary_session_);
        }
        if (teardown_state_ == TeardownState::RUNNING) {
          rx_queue_->TriggerSessionChanged();
        }
      }

      active_primary_sessions_--;
      should_stop = active_primary_sessions_ == 0;
    }
  }

  // stop the device, no more sessions are running.
  if (should_stop) {
    StopDevice();
  }
}

void DeviceInterface::StartDevice() {
  LOG_TRACE("network-device: StartDevice");

  bool start = false;
  tx_queue_->Lock();
  rx_queue_->Lock();
  // Start the device if we haven't done so already.
  switch (device_status_) {
    case DeviceStatus::STARTED:
    case DeviceStatus::STARTING:
      // Remove any pending operations we may have.
      pending_device_op_ = PendingDeviceOperation::NONE;
      break;
    case DeviceStatus::STOPPING:
      // Device is currently stopping, let's record that we want to start it.
      pending_device_op_ = PendingDeviceOperation::START;
      break;
    case DeviceStatus::STOPPED:
      // Device is in STOPPED state, start it.
      device_status_ = DeviceStatus::STARTING;
      start = true;
      break;
  }

  rx_queue_->Unlock();
  tx_queue_->Unlock();

  if (start) {
    StartDeviceInner();
  }
}

void DeviceInterface::StartDeviceInner() {
  LOG_TRACE("network-device: StartDeviceInner");
  device_.Start([](void* cookie) { reinterpret_cast<DeviceInterface*>(cookie)->DeviceStarted(); },
                this);
}

void DeviceInterface::StopDevice() {
  LOG_TRACE("network-device: StopDevice");

  tx_queue_->Lock();
  rx_queue_->Lock();
  bool stop = false;
  switch (device_status_) {
    case DeviceStatus::STOPPED:
    case DeviceStatus::STOPPING:
      // Remove any pending operations we may have.
      pending_device_op_ = PendingDeviceOperation::NONE;
      break;
    case DeviceStatus::STARTING:
      // Device is currently starting, let's record that we want to stop it.
      pending_device_op_ = PendingDeviceOperation::STOP;
      break;
    case DeviceStatus::STARTED:
      // Device is in STARTED state, stop it.
      device_status_ = DeviceStatus::STOPPING;
      stop = true;
  }
  rx_queue_->Unlock();
  tx_queue_->Unlock();
  if (stop) {
    StopDeviceInner();
  }
}

void DeviceInterface::StopDeviceInner() {
  LOG_TRACE("network-device: StopDeviceInner");
  device_.Stop([](void* cookie) { reinterpret_cast<DeviceInterface*>(cookie)->DeviceStopped(); },
               this);
}

PendingDeviceOperation DeviceInterface::SetDeviceStatus(DeviceStatus status) {
  tx_queue_->Lock();
  rx_queue_->Lock();
  auto pending_op = pending_device_op_;
  device_status_ = status;
  pending_device_op_ = PendingDeviceOperation::NONE;
  if (status == DeviceStatus::STOPPED) {
    tx_queue_->Reclaim();
    rx_queue_->Reclaim();
  }
  tx_queue_->Unlock();
  rx_queue_->Unlock();
  return pending_op;
}

void DeviceInterface::DeviceStarted() {
  LOG_TRACE("network-device: DeviceStarted");

  auto pending_op = SetDeviceStatus(DeviceStatus::STARTED);
  if (pending_op == PendingDeviceOperation::STOP) {
    StopDevice();
    return;
  }

  NotifyTxQueueAvailable();
  // notify Rx queue that the device has started
  rx_queue_->TriggerRxWatch();
}

void DeviceInterface::DeviceStopped() {
  LOG_TRACE("network-device: DeviceStopped");
  teardown_lock_.Acquire();
  auto pending_op = SetDeviceStatus(DeviceStatus::STOPPED);

  if (ContinueTeardown(TeardownState::SESSIONS)) {
    return;
  }

  if (pending_op == PendingDeviceOperation::START) {
    StartDevice();
  }
}

bool DeviceInterface::ContinueTeardown(network::internal::DeviceInterface::TeardownState state) {
  // The teardown process goes through different phases, encoded by the TeradownState enumeration.
  // - RUNNING: no teardown is in process. We move out of the RUNNING state by calling Unbind on all
  // the DeviceInterface's bindings.
  // - BINDINGS: Waiting for all bindings to close. Only moves to next state once all bindings are
  // closed, then calls unbind on all watchers and moves to the WATCHERS state.
  // - WATCHERS: Waiting for all watcher bindings to close. Only moves to the next state once all
  // watchers are closed, then proceeds to stop and destroy all sessions.
  // - SESSIONS: Waiting for all sessions to be closed and destroyed (dead or alive). This is the
  // final stage, once all the sessions are properly destroyed the teardown_callback_ will be
  // triggered, marking the end of the teardown process.
  //
  // To protect the linearity of the teardown process, once it has started (the state is no longer
  // RUNNING) no more bindings, watchers, or sessions can be created.

  bool do_teardown = [this, state]() {
    if (state != teardown_state_) {
      return false;
    }
    switch (teardown_state_) {
      case TeardownState::RUNNING: {
        fbl::AutoLock bindings_lock(&bindings_lock_);
        teardown_state_ = TeardownState::BINDINGS;
        LOGF_TRACE("network-device: Teardown state is BINDINGS (%ld bindings to destroy)",
                   bindings_.size_slow());
        if (!bindings_.is_empty()) {
          for (auto& b : bindings_) {
            b.Unbind();
          }
          return false;
        }
        // Let fallthrough, no bindings to destroy.
      }
      case TeardownState::BINDINGS: {
        {
          fbl::AutoLock bindings_lock(&bindings_lock_);
          // Pre-condition to enter watchers state: bindings must be empty.
          if (!bindings_.is_empty()) {
            return false;
          }
        }
        fbl::AutoLock watchers_lock(&watchers_lock_);
        teardown_state_ = TeardownState::WATCHERS;
        LOGF_TRACE("network-device: Teardown state is WATCHERS (%ld watchers to destroy)",
                   watchers_.size_slow());
        if (!watchers_.is_empty()) {
          for (auto& w : watchers_) {
            w.Unbind();
          }
          return false;
        }
        // Let it fallthrough, no watchers to destroy.
      }
      case TeardownState::WATCHERS: {
        {
          fbl::AutoLock watchers_lock(&watchers_lock_);
          // Pre-condition to enter sessions state: watchers must be empty.
          if (!watchers_.is_empty()) {
            return false;
          }
        }
        fbl::AutoLock sessions_lock(&sessions_lock_);
        teardown_state_ = TeardownState::SESSIONS;
        LOG_TRACE("network-device: Teardown state is SESSIONS");
        if (primary_session_ || !sessions_.is_empty()) {
          // If we have any sessions, signal all of them to stop their threads callback. Each
          // session that finishes operating will go through the `NotifyDeadSession` machinery. The
          // teardown is only complete when all sessions are destroyed.
          LOG_TRACE("network-device: Teardown: sessions are running, scheduling teardown");
          if (primary_session_) {
            primary_session_->Kill();
          }
          for (auto& s : sessions_) {
            s.Kill();
          }
          // We won't check for dead sessions here, since all the sessions we just called `Kill` on
          // will go into the dead state asynchronously. Any sessions that are already in the dead
          // state will also get checked in `PruneDeadSessions` at a later time.
          return false;
        }
        // No sessions are alive. Now check if we have any dead sessions that are waiting to reclaim
        // buffers.
        fbl::AutoLock dead_sessions(&dead_sessions_lock_);
        if (!dead_sessions_.is_empty()) {
          LOG_TRACE("network-device: Teardown: dead sessions pending, waiting for teardown");
          // We need to wait for the device to safely give us all the buffers back before completing
          // the teardown.
          return false;
        }
        // We can teardown immediately, let it fall through
      }
      case TeardownState::SESSIONS: {
        fbl::AutoLock sessions_lock(&sessions_lock_);
        fbl::AutoLock dead_sessions_lock(&dead_sessions_lock_);
        // Lock the queues to safely access the device status.
        tx_queue_->Lock();
        rx_queue_->Lock();
        // Condition to finish teardown: no more sessions exists (dead or alive) and the device
        // state is STOPPED.
        if (sessions_.is_empty() && !primary_session_ && dead_sessions_.is_empty() &&
            device_status_ == DeviceStatus::STOPPED) {
          teardown_state_ = TeardownState::FINISHED;
          LOG_TRACE("network-device: Teardown finished");
          dead_sessions_lock.release();
          sessions_lock.release();
          rx_queue_->Unlock();
          tx_queue_->Unlock();
          return true;
        } else {
          rx_queue_->Unlock();
          tx_queue_->Unlock();
          LOG_TRACE("network-device: Teardown: Still pending sessions teardown");
          return false;
        }
      }
      case TeardownState::FINISHED:
        ZX_PANIC("Nothing to do if the teardown state is finished.");
        return true;
    }
  }();

  if (do_teardown) {
    auto callback = std::move(teardown_callback_);
    teardown_lock_.Release();
    callback();
    return true;
  }

  teardown_lock_.Release();
  return false;
}

void DeviceInterface::ReleaseVmo(Session* session) {
  uint8_t vmo;
  {
    fbl::AutoLock lock(&vmos_lock_);
    vmo = session->ReleaseDataVmo();
    zx_status_t status = vmo_store_.Unregister(vmo);
    if (status != ZX_OK) {
      // Avoid notifying the device implementation if unregistration fails.
      // A non-ok return here means we're either attempting to double-release a VMO or the sessions
      // didn't have a registered VMO.
      LOGF_WARN("network-device(%s): Failed to unregister VMO %d: %s", session->name(), vmo,
                zx_status_get_string(status));
      return;
    }
  }
  device_.ReleaseVmo(vmo);
}

fbl::RefPtr<RefCountedFifo> DeviceInterface::primary_rx_fifo() {
  fbl::AutoLock lock(&sessions_lock_);
  if (primary_session_) {
    return primary_session_->rx_fifo();
  } else {
    return nullptr;
  }
}

void DeviceInterface::NotifyTxQueueAvailable() {
  fbl::AutoLock lock(&sessions_lock_);
  if (primary_session_) {
    primary_session_->ResumeTx();
  }
  for (auto& session : sessions_) {
    session.ResumeTx();
  }
}

void DeviceInterface::QueueRxSpace(const rx_space_buffer_t* rx, size_t count) {
  device_.QueueRxSpace(rx, count);
}

void DeviceInterface::QueueTx(const tx_buffer_t* tx, size_t count) { device_.QueueTx(tx, count); }

void DeviceInterface::NotifyDeadSession(Session* dead_session) {
  LOGF_TRACE("network-device: NotifyDeadSession '%s'", dead_session->name());
  // First of all, stop all data-plane operations with stopped session.
  if (!dead_session->IsPaused()) {
    // Stop the session
    SessionStopped(dead_session);
  }
  if (dead_session->IsPrimary()) {
    // Tell rx queue this session can't be used anymore
    rx_queue_->PurgeSession(dead_session);
  }

  std::unique_ptr<Session> session_ptr;
  // Lock the teardown state during the next critical session to avoid races with the DeviceStop
  // that can be triggered by sessions.
  teardown_lock_.Acquire();

  // Now find it in sessions and remove it.
  fbl::AutoLock lock(&sessions_lock_);

  if (dead_session == primary_session_.get()) {
    // Nullify primary session.
    session_ptr = std::move(primary_session_);
  } else {
    session_ptr = sessions_.erase(*dead_session);
  }

  // we can destroy the session immediately.
  if (session_ptr->CanDestroy()) {
    LOGF_TRACE("network-device: NotifyDeadSession '%s' destroying session", dead_session->name());
    ReleaseVmo(session_ptr.get());
    session_ptr = nullptr;
    lock.release();
    ContinueTeardown(TeardownState::SESSIONS);
    return;
  }
  teardown_lock_.Release();

  fbl::AutoLock d_lock(&dead_sessions_lock_);

  // otherwise, add it to the list of dead sessions so we can wait for buffers to be returned before
  // destroying it.
  LOGF_TRACE(
      "network-device: NotifyDeadSession: session '%s' is dead, waiting for buffers to be "
      "reclaimed",
      session_ptr->name());
  dead_sessions_.push_back(std::move(session_ptr));
}

void DeviceInterface::PruneDeadSessions() {
  fbl::AutoLock lock(&dead_sessions_lock_);
  auto it = dead_sessions_.begin();
  while (it != dead_sessions_.end()) {
    auto& session = *it;
    // increment iterator before erasing, because of DoublyLinkedList
    ++it;
    if (session.CanDestroy()) {
      LOGF_TRACE("network-device: PruneDeadSessions: destroying %s", session.name());
      ReleaseVmo(&session);
      dead_sessions_.erase(session);
    } else {
      LOGF_TRACE("network-device: PruneDeadSessions: %s still pending", session.name());
    }
  }
}

zx_status_t DeviceInterface::RegisterDataVmo(zx::vmo vmo, uint8_t* out_id,
                                             DataVmoStore::StoredVmo** out_stored_vmo) {
  zx::vmo device_vmo;
  {
    fbl::AutoLock lock_vmos(&vmos_lock_);
    if (vmo_store_.is_full()) {
      return ZX_ERR_NO_RESOURCES;
    }
    // Duplicate the VMO to share with device implementation.
    zx_status_t status;
    if ((status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &device_vmo)) != ZX_OK) {
      return status;
    }

    auto result = vmo_store_.Register(std::move(vmo));
    if (result.is_error()) {
      return result.error();
    }
    *out_id = result.value();
    *out_stored_vmo = vmo_store_.GetVmo(*out_id);
  }

  device_.PrepareVmo(*out_id, std::move(device_vmo));
  return ZX_OK;
}

void DeviceInterface::CommitAllSessions() {
  {
    fbl::AutoLock lock(&sessions_lock_);
    if (primary_session_) {
      primary_session_->CommitRx();
    }
    for (auto& session : sessions_) {
      session.CommitRx();
    }
  }
  PruneDeadSessions();
}

void DeviceInterface::CopySessionData(const Session& owner, uint16_t owner_index,
                                      const rx_buffer_t* buff) {
  fbl::AutoLock lock(&sessions_lock_);
  if (primary_session_ && primary_session_.get() != &owner) {
    primary_session_->CompleteRxWith(owner, owner_index, buff);
  }

  for (auto& session : sessions_) {
    if (&session != &owner) {
      session.CompleteRxWith(owner, owner_index, buff);
    }
  }
}

bool DeviceInterface::ListenSessionData(const Session& owner, uint16_t owner_index) {
  if ((device_info_.device_features & FEATURE_NO_AUTO_SNOOP) ||
      !has_listen_sessions_.load(std::memory_order_relaxed)) {
    // avoid locking the sessions mutex if we don't have any sessions interested in this
    return false;
  }
  fbl::AutoLock lock(&sessions_lock_);
  bool copied = false;
  if (primary_session_ && primary_session_.get() != &owner && primary_session_->IsListen()) {
    copied |= primary_session_->ListenFromTx(owner, owner_index);
  }
  for (auto& s : sessions_) {
    if (&s != &owner && s.IsListen()) {
      copied |= s.ListenFromTx(owner, owner_index);
    }
  }
  return copied;
}

zx_status_t DeviceInterface::LoadRxDescriptors(RxQueue::SessionTransaction* transact) {
  fbl::AutoLock lock(&sessions_lock_);
  if (!primary_session_) {
    return ZX_ERR_BAD_STATE;
  }
  return primary_session_->LoadRxDescriptors(transact);
}

bool DeviceInterface::IsValidRxFrameType(uint8_t frame_type) const {
  const auto* end = &device_info_.rx_types_list[device_info_.rx_types_count];
  for (const auto* rx_types = device_info_.rx_types_list; rx_types != end; rx_types++) {
    if (*rx_types == frame_type) {
      return true;
    }
  }
  return false;
}

bool DeviceInterface::IsValidTxFrameType(uint8_t frame_type) const {
  const auto* end = &device_info_.tx_types_list[device_info_.tx_types_count];
  for (const auto* tx_types = device_info_.tx_types_list; tx_types != end; tx_types++) {
    if (tx_types->type == frame_type) {
      return true;
    }
  }
  return false;
}

bool DeviceInterface::IsDataPlaneOpen() { return device_status_ == DeviceStatus::STARTED; }

zx_status_t DeviceInterface::Binding::Bind(DeviceInterface* interface, zx::channel channel) {
  fbl::AllocChecker ac;
  std::unique_ptr<Binding> binding(new (&ac) Binding);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto* binding_ptr = binding.get();
  auto result = fidl::BindServer(
      interface->dispatcher_, std::move(channel), interface,
      fidl::OnUnboundFn<DeviceInterface>(
          [binding_ptr](DeviceInterface* interface, fidl::UnbindInfo, zx::channel) {
            bool bindings_empty;
            interface->teardown_lock_.Acquire();
            {
              fbl::AutoLock lock(&interface->bindings_lock_);
              interface->bindings_.erase(*binding_ptr);
              bindings_empty = interface->bindings_.is_empty();
            }

            if (bindings_empty) {
              interface->ContinueTeardown(TeardownState::BINDINGS);
            } else {
              interface->teardown_lock_.Release();
            }
          }));
  if (result.is_ok()) {
    binding->binding_ = result.take_value();
    fbl::AutoLock lock(&interface->bindings_lock_);
    interface->bindings_.push_front(std::move(binding));
    return ZX_OK;
  } else {
    return result.error();
  }
}

void DeviceInterface::Binding::Unbind() {
  auto binding = std::move(binding_);
  if (binding.has_value()) {
    binding->Unbind();
  }
}

}  // namespace internal
}  // namespace network
