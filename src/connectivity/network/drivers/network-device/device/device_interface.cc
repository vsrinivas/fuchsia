// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_interface.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/defer.h>
#include <zircon/device/network.h>

#include <fbl/alloc_checker.h>

#include "log.h"
#include "rx_queue.h"
#include "session.h"
#include "tx_queue.h"

// Static sanity assertions from far-away defined buffer_descriptor_t.
// A buffer descriptor is always described in 64 bit words.
static_assert(sizeof(buffer_descriptor_t) % 8 == 0);
// Verify no unseen padding is being added by the compiler and all padding reservation fields are
// working as expected, check the offset of every 64 bit word in the struct.
static_assert(offsetof(buffer_descriptor_t, frame_type) == 0);
static_assert(offsetof(buffer_descriptor_t, port_id) == 8);
static_assert(offsetof(buffer_descriptor_t, offset) == 16);
static_assert(offsetof(buffer_descriptor_t, head_length) == 24);
static_assert(offsetof(buffer_descriptor_t, inbound_flags) == 32);
// Descriptor length is reported as uint8 words in session info, make sure that fits.
static_assert(sizeof(buffer_descriptor_t) / sizeof(uint64_t) < std::numeric_limits<uint8_t>::max());

namespace {
const char* DeviceStatusToString(network::internal::DeviceStatus status) {
  switch (status) {
    case network::internal::DeviceStatus::STARTING:
      return "STARTING";
    case network::internal::DeviceStatus::STARTED:
      return "STARTED";
    case network::internal::DeviceStatus::STOPPING:
      return "STOPPING";
    case network::internal::DeviceStatus::STOPPED:
      return "STOPPED";
  }
}
}  // namespace

namespace network {

zx::result<std::unique_ptr<NetworkDeviceInterface>> NetworkDeviceInterface::Create(
    async_dispatcher_t* dispatcher, ddk::NetworkDeviceImplProtocolClient parent) {
  return internal::DeviceInterface::Create(dispatcher, parent);
}

namespace internal {

uint16_t TransformFifoDepth(uint16_t device_depth) {
  // We're going to say the depth is twice the depth of the device to account for in-flight
  // buffers, as long as it doesn't go over the maximum fifo depth.

  // Check for overflow.
  if (device_depth > (std::numeric_limits<uint16_t>::max() >> 1)) {
    return kMaxFifoDepth;
  }

  return std::min(kMaxFifoDepth, static_cast<uint16_t>(device_depth << 1));
}

zx::result<std::unique_ptr<DeviceInterface>> DeviceInterface::Create(
    async_dispatcher_t* dispatcher, ddk::NetworkDeviceImplProtocolClient parent) {
  fbl::AllocChecker ac;
  std::unique_ptr<DeviceInterface> device(new (&ac) DeviceInterface(dispatcher, parent));
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  zx_status_t status = device->Init();
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(device));
}

DeviceInterface::~DeviceInterface() {
  ZX_ASSERT_MSG(primary_session_ == nullptr,
                "can't destroy DeviceInterface with active primary session. (%s)",
                primary_session_->name());
  ZX_ASSERT_MSG(sessions_.is_empty(), "can't destroy DeviceInterface with %ld pending session(s).",
                sessions_.size());
  ZX_ASSERT_MSG(dead_sessions_.is_empty(),
                "can't destroy DeviceInterface with %ld pending dead session(s).",
                dead_sessions_.size());
  ZX_ASSERT_MSG(bindings_.is_empty(), "can't destroy device interface with %ld attached bindings.",
                bindings_.size());
  size_t active_ports = std::count_if(ports_.begin(), ports_.end(),
                                      [](const PortSlot& port) { return port.port != nullptr; });
  ZX_ASSERT_MSG(!active_ports, "can't destroy device interface with %ld ports", active_ports);
}

zx_status_t DeviceInterface::Init() {
  LOGF_TRACE("%s", __FUNCTION__);
  if (!device_.is_valid()) {
    LOG_ERROR("init: no protocol");
    return ZX_ERR_INTERNAL;
  }

  network_device_impl_protocol_t proto;
  device_.GetProto(&proto);
  if (proto.ops == nullptr) {
    LOG_ERROR("init: null protocol ops");
    return ZX_ERR_INTERNAL;
  }
  const network_device_impl_protocol_ops_t& ops = *proto.ops;
  if (ops.init == nullptr || ops.get_info == nullptr || ops.stop == nullptr ||
      ops.start == nullptr || ops.queue_tx == nullptr || ops.queue_rx_space == nullptr ||
      ops.prepare_vmo == nullptr || ops.release_vmo == nullptr || ops.set_snoop == nullptr) {
    LOGF_ERROR("init: device: incomplete protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  device_.GetInfo(&device_info_);
  if (device_info_.buffer_alignment == 0) {
    LOGF_ERROR("init: device reports invalid zero buffer alignment");
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (device_info_.rx_threshold > device_info_.rx_depth) {
    LOGF_ERROR("init: device reports rx_threshold = %d larger than rx_depth %d",
               device_info_.rx_threshold, device_info_.rx_depth);
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (device_info_.rx_accel_count > netdev::wire::kMaxAccelFlags ||
      device_info_.tx_accel_count > netdev::wire::kMaxAccelFlags) {
    LOGF_ERROR("init: device reports too many acceleration flags");
    return ZX_ERR_NOT_SUPPORTED;
  }
  // Copy the vectors of supported acceleration flags.
  {
    cpp20::span span(device_info_.rx_accel_list, device_info_.rx_accel_count);
    std::transform(span.begin(), span.end(), accel_rx_.begin(),
                   [](uint8_t v) { return static_cast<netdev::wire::RxAcceleration>(v); });
  }
  {
    cpp20::span span(device_info_.tx_accel_list, device_info_.tx_accel_count);
    std::transform(span.begin(), span.end(), accel_tx_.begin(),
                   [](uint8_t v) { return static_cast<netdev::wire::TxAcceleration>(v); });
  }
  // Clear list accessors -- they point to device-owned memory. We can access the lists through the
  // |accel_rx_| and |accel_tx_| member fields.
  device_info_.rx_accel_list = nullptr;
  device_info_.tx_accel_list = nullptr;

  if (device_info_.rx_depth > kMaxFifoDepth || device_info_.tx_depth > kMaxFifoDepth) {
    LOGF_ERROR("init: device reports too large FIFO depths: %d/%d (max=%d)", device_info_.rx_depth,
               device_info_.tx_depth, kMaxFifoDepth);
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx::result tx_queue = TxQueue::Create(this);
  if (tx_queue.is_error()) {
    LOGF_ERROR("init: device failed to start Tx Queue: %s", tx_queue.status_string());
    return tx_queue.status_value();
  }
  tx_queue_ = std::move(tx_queue.value());

  zx::result rx_queue = RxQueue::Create(this);
  if (rx_queue.is_error()) {
    LOGF_ERROR("init: device failed to start Rx Queue: %s", rx_queue.status_string());
    return rx_queue.status_value();
  }
  rx_queue_ = std::move(rx_queue.value());

  zx_status_t status;
  {
    fbl::AutoLock lock(&control_lock_);
    if ((status = vmo_store_.Reserve(MAX_VMOS)) != ZX_OK) {
      LOGF_ERROR("init: failed to init session identifiers %s", zx_status_get_string(status));
      return status;
    }
  }

  // Init session with parent.
  if ((status = device_.Init(this, &network_device_ifc_protocol_ops_)) != ZX_OK) {
    LOGF_ERROR("init: NetworkDevice Init failed: %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

void DeviceInterface::Teardown(fit::callback<void()> teardown_callback) {
  // stop all rx queue operation immediately.
  rx_queue_->JoinThread();
  tx_queue_->JoinThread();
  LOGF_TRACE("%s", __FUNCTION__);

  control_lock_.Acquire();
  // Can't call teardown again until the teardown process has ended.
  ZX_ASSERT(teardown_callback_ == nullptr);
  teardown_callback_ = std::move(teardown_callback);

  ContinueTeardown(TeardownState::RUNNING);
}

zx_status_t DeviceInterface::Bind(fidl::ServerEnd<netdev::Device> req) {
  fbl::AutoLock lock(&control_lock_);
  // Don't attach new bindings if we're tearing down.
  if (teardown_state_ != TeardownState::RUNNING) {
    return ZX_ERR_BAD_STATE;
  }
  return Binding::Bind(this, std::move(req));
}

zx_status_t DeviceInterface::BindPort(uint8_t port_id, fidl::ServerEnd<netdev::Port> req) {
  fbl::AutoLock lock(&control_lock_);
  if (teardown_state_ != TeardownState::RUNNING) {
    return ZX_ERR_BAD_STATE;
  }
  if (port_id >= MAX_PORTS) {
    return ZX_ERR_NOT_FOUND;
  }
  PortSlot& slot = ports_[port_id];
  if (slot.port == nullptr) {
    return ZX_ERR_NOT_FOUND;
  }
  slot.port->Bind(std::move(req));
  return ZX_OK;
}

void DeviceInterface::NetworkDeviceIfcPortStatusChanged(uint8_t port_id,
                                                        const port_status_t* new_status) {
  SharedAutoLock lock(&control_lock_);
  // Skip port status changes if tearing down. During teardown ports may disappear and device
  // implementation may not be aware of it yet.
  if (teardown_state_ != TeardownState::RUNNING) {
    return;
  }
  WithPort(port_id, [&new_status, port_id](const std::unique_ptr<DevicePort>& port) {
    if (!port) {
      LOGF_ERROR("StatusChanged on unknown port=%d %d %d", port_id, new_status->flags,
                 new_status->mtu);
      return;
    }

    LOGF_TRACE("StatusChanged(port=%d) %d %d", port_id, new_status->flags, new_status->mtu);
    port->StatusChanged(*new_status);
  });
}

void DeviceInterface::NetworkDeviceIfcAddPort(uint8_t port_id,
                                              const network_port_protocol_t* port_proto) {
  LOGF_TRACE("%s(%d)", __FUNCTION__, port_id);
  auto port_client = ddk::NetworkPortProtocolClient(port_proto);
  auto release_port = fit::defer([&port_client]() {
    if (port_client.is_valid()) {
      port_client.Removed();
    }
  });

  // Creating a MacAddrDeviceInterface may interact with the device, do that without holding the
  // control lock to prevent deadlocks
  std::unique_ptr<MacAddrDeviceInterface> mac;
  mac_addr_protocol_t mac_proto;
  port_client.GetMac(&mac_proto);
  ddk::MacAddrProtocolClient mac_client(&mac_proto);
  if (mac_client.is_valid()) {
    zx::result status = MacAddrDeviceInterface::Create(mac_client);
    if (status.is_error()) {
      LOGF_ERROR("failed to instantiate MAC information for port %d: %s", port_id,
                 status.status_string());
      return;
    }
    mac = std::move(status.value());
  }

  fbl::AutoLock lock(&control_lock_);
  // Don't allow new ports if tearing down.
  if (teardown_state_ != TeardownState::RUNNING) {
    LOGF_WARN("port %d not added, teardown in progress", port_id);
    return;
  }
  if (port_id >= ports_.size()) {
    LOGF_ERROR("port id %d out of allowed range: [0, %ld)", port_id, ports_.size());
    return;
  }
  PortSlot& port_slot = ports_[port_id];
  if (port_slot.port != nullptr) {
    LOGF_ERROR("port %d already exists", port_id);
    return;
  }

  fbl::AllocChecker checker;
  const netdev::wire::PortId salted_id = {
      .base = port_id,
      // NB: This relies on wrapping overflow.
      .salt = static_cast<uint8_t>(port_slot.salt + 1),
  };
  std::unique_ptr<DevicePort> port(
      new (&checker) DevicePort(this, dispatcher_, salted_id, port_client, std::move(mac),
                                fit::bind_member<&DeviceInterface::OnPortTeardownComplete>(this)));
  if (!checker.check()) {
    LOGF_ERROR("failed to allocate port memory");
    return;
  }

  // Clear port_client to prevent deferred call from notifying removal.
  port_client.clear();
  // Update slot with newly created port and its salt.
  port_slot.salt = salted_id.salt;
  port_slot.port = std::move(port);

  for (auto& watcher : port_watchers_) {
    watcher.PortAdded(salted_id);
  }
}

void DeviceInterface::NetworkDeviceIfcRemovePort(uint8_t port_id) {
  LOGF_TRACE("%s(%d)", __FUNCTION__, port_id);
  SharedAutoLock lock(&control_lock_);
  // Ignore if we're tearing down, all ports will be removed as part of teardown.
  if (teardown_state_ != TeardownState::RUNNING) {
    return;
  }
  WithPort(port_id,
           [this](const std::unique_ptr<DevicePort>& port) __TA_REQUIRES_SHARED(control_lock_) {
             if (port) {
               for (auto& watcher : port_watchers_) {
                 watcher.PortRemoved(port->id());
               }
               port->Teardown();
             }
           });
}

void DeviceInterface::NetworkDeviceIfcCompleteRx(const rx_buffer_t* rx_list, size_t rx_count) {
  LOGF_TRACE("%s(_, %ld)", __FUNCTION__, rx_count);
  rx_queue_->CompleteRxList(rx_list, rx_count);
}

void DeviceInterface::NetworkDeviceIfcCompleteTx(const tx_result_t* tx_list, size_t tx_count) {
  LOGF_TRACE("%s(_, %ld)", __FUNCTION__, tx_count);
  tx_queue_->CompleteTxList(tx_list, tx_count);
}

void DeviceInterface::NetworkDeviceIfcSnoop(const rx_buffer_t* rx_list, size_t rx_count) {
  // TODO(fxbug.dev/43028): Implement real version. Current implementation acts as if no LISTEN is
  // ever in place.
}

void DeviceInterface::GetInfo(GetInfoCompleter::Sync& completer) {
  LOGF_TRACE("%s", __FUNCTION__);
  fidl::WireTableFrame<netdev::wire::DeviceInfo> frame;
  netdev::wire::DeviceInfo device_info(
      fidl::ObjectView<fidl::WireTableFrame<netdev::wire::DeviceInfo>>::FromExternal(&frame));

  uint8_t min_descriptor_length = sizeof(buffer_descriptor_t) / sizeof(uint64_t);
  uint8_t descriptor_version = NETWORK_DEVICE_DESCRIPTOR_VERSION;
  uint16_t rx_depth = rx_fifo_depth();
  uint16_t tx_depth = tx_fifo_depth();
  auto tx_accel = fidl::VectorView<netdev::wire::TxAcceleration>::FromExternal(
      accel_tx_.data(), device_info_.tx_accel_count);
  auto rx_accel = fidl::VectorView<netdev::wire::RxAcceleration>::FromExternal(
      accel_rx_.data(), device_info_.rx_accel_count);
  device_info.set_min_descriptor_length(min_descriptor_length)
      .set_descriptor_version(descriptor_version)
      .set_rx_depth(rx_depth)
      .set_tx_depth(tx_depth)
      .set_buffer_alignment(device_info_.buffer_alignment)
      .set_max_buffer_parts(device_info_.max_buffer_parts)
      .set_min_rx_buffer_length(device_info_.min_rx_buffer_length)
      .set_min_tx_buffer_length(device_info_.min_tx_buffer_length)
      .set_min_tx_buffer_head(device_info_.tx_head_length)
      .set_min_tx_buffer_tail(device_info_.tx_tail_length)
      .set_tx_accel(fidl::ObjectView<decltype(tx_accel)>::FromExternal(&tx_accel))
      .set_rx_accel(fidl::ObjectView<decltype(rx_accel)>::FromExternal(&rx_accel));

  if (device_info_.max_buffer_length != 0) {
    device_info.set_max_buffer_length(device_info_.max_buffer_length);
  }

  completer.Reply(std::move(device_info));
}

void DeviceInterface::OpenSession(OpenSessionRequestView request,
                                  OpenSessionCompleter::Sync& completer) {
  zx::result sync_result = [this, &request]()
      -> zx::result<std::tuple<netdev::wire::DeviceOpenSessionResponse, uint8_t, zx::vmo>> {
    fbl::AutoLock tx_lock(&tx_lock_);
    fbl::AutoLock lock(&control_lock_);
    // We're currently tearing down and can't open any new sessions.
    if (teardown_state_ != TeardownState::RUNNING) {
      return zx::error(ZX_ERR_UNAVAILABLE);
    }

    zx::result endpoints = fidl::CreateEndpoints<netdev::Session>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }

    fidl::StringView& name = request->session_name;
    netdev::wire::SessionInfo& session_info = request->session_info;
    zx::result session_creation =
        Session::Create(dispatcher_, session_info, name, this, std::move(endpoints->server));
    if (session_creation.is_error()) {
      return session_creation.take_error();
    }
    auto& [session, fifos] = session_creation.value();

    if (!session_info.has_data()) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    zx::vmo& vmo = session_info.data();
    // NB: It's safe to register the VMO after session creation (and thread start) because sessions
    // always start in a paused state, so the tx path can't be running while we hold the control
    // lock.
    if (vmo_store_.is_full()) {
      return zx::error(ZX_ERR_NO_RESOURCES);
    }
    // Duplicate the VMO to share with the device implementation.
    zx::vmo device_vmo;
    if (zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &device_vmo); status != ZX_OK) {
      return zx::error(status);
    }

    zx::result registration = vmo_store_.Register(std::move(vmo));
    if (registration.is_error()) {
      return registration.take_error();
    }
    const uint8_t vmo_id = registration.value();
    session->SetDataVmo(vmo_id, vmo_store_.GetVmo(vmo_id));
    session->AssertParentTxLock(*this);
    session->InstallTx();

    if (session->ShouldTakeOverPrimary(primary_session_.get())) {
      // Set this new session as the primary session.
      std::swap(primary_session_, session);
      rx_queue_->TriggerSessionChanged();
    }
    if (session) {
      // Add the new session (or the primary session if it the new session just took over) to
      // the list of sessions.
      sessions_.push_back(std::move(session));
    }

    return zx::ok(std::make_tuple(
        netdev::wire::DeviceOpenSessionResponse{
            .session = std::move(endpoints->client),
            .fifos = std::move(fifos),
        },
        vmo_id, std::move(device_vmo)));
  }();

  if (sync_result.is_error()) {
    completer.ReplyError(sync_result.error_value());
    return;
  }

  // Holds information about a pending session creation.
  //
  // By holding the client end on this pending operation, we guarantee that if
  // we drop it the session will be closed and thus the regular cleanup path is
  // triggered.
  struct PendingSessionOpen {
    PendingSessionOpen(netdev::wire::DeviceOpenSessionResponse response,
                       OpenSessionCompleter::Sync& completer)
        : response(std::move(response)), completer(completer.ToAsync()) {}
    netdev::wire::DeviceOpenSessionResponse response;
    OpenSessionCompleter::Async completer;
  };
  auto [response, vmo_id, device_vmo] = std::move(sync_result.value());

  // NB: PendingSessionOpen's constructor takes over the completer on successful
  // allocation.
  fbl::AllocChecker ac;
  std::unique_ptr cookie =
      fbl::make_unique_checked<PendingSessionOpen>(&ac, std::move(response), completer);
  if (!ac.check()) {
    completer.ReplyError(ZX_ERR_NO_MEMORY);
    return;
  }
  device_.PrepareVmo(
      vmo_id, std::move(device_vmo),
      [](void* cookie, zx_status_t status) {
        std::unique_ptr<PendingSessionOpen> pending_open(static_cast<PendingSessionOpen*>(cookie));
        if (status != ZX_OK) {
          LOGF_ERROR("failed to prepare vmo: %s", zx_status_get_string(status));
          pending_open->completer.ReplyError(ZX_ERR_INTERNAL);
          return;
        }

        pending_open->completer.ReplySuccess(std::move(pending_open->response.session),
                                             std::move(pending_open->response.fifos));
      },
      cookie.release());
}

void DeviceInterface::GetPort(GetPortRequestView request, GetPortCompleter::Sync& _completer) {
  SharedAutoLock lock(&control_lock_);
  WithPort(request->id.base, [req = std::move(request->port), salt = request->id.salt](
                                 const std::unique_ptr<DevicePort>& port) mutable {
    if (port && port->id().salt == salt) {
      port->Bind(std::move(req));
    } else {
      req.Close(ZX_ERR_NOT_FOUND);
    }
  });
}

void DeviceInterface::GetPortWatcher(GetPortWatcherRequestView request,
                                     GetPortWatcherCompleter::Sync& _completer) {
  fbl::AutoLock lock(&control_lock_);
  if (teardown_state_ != TeardownState::RUNNING) {
    // Don't install new watchers after teardown has started.
    return;
  }

  fbl::AllocChecker ac;
  auto watcher = fbl::make_unique_checked<PortWatcher>(&ac);
  if (!ac.check()) {
    request->watcher.Close(ZX_ERR_NO_MEMORY);
    return;
  }

  std::array<netdev::wire::PortId, MAX_PORTS> port_ids;
  size_t port_id_count = 0;

  for (const PortSlot& port : ports_) {
    if (port.port) {
      port_ids[port_id_count++] = port.port->id();
    }
  }

  zx_status_t status = watcher->Bind(dispatcher_, cpp20::span(port_ids.begin(), port_id_count),
                                     std::move(request->watcher), [this](PortWatcher& watcher) {
                                       fbl::AutoLock lock(&control_lock_);
                                       port_watchers_.erase(watcher);
                                       ContinueTeardown(TeardownState::PORT_WATCHERS);
                                     });

  if (status != ZX_OK) {
    LOGF_ERROR("failed to bind port watcher: %s", zx_status_get_string(status));
    return;
  }
  port_watchers_.push_back(std::move(watcher));
}

void DeviceInterface::Clone(CloneRequestView request, CloneCompleter::Sync& _completer) {
  if (zx_status_t status = Bind(std::move(request->device)); status != ZX_OK) {
    LOGF_ERROR("bind failed %s", zx_status_get_string(status));
  }
}

uint16_t DeviceInterface::rx_fifo_depth() const {
  return TransformFifoDepth(device_info_.rx_depth);
}

uint16_t DeviceInterface::tx_fifo_depth() const {
  return TransformFifoDepth(device_info_.tx_depth);
}

void DeviceInterface::SessionStarted(Session& session) {
  bool should_start = false;
  if (session.IsListen()) {
    has_listen_sessions_.store(true, std::memory_order_relaxed);
  }
  if (session.IsPrimary()) {
    active_primary_sessions_++;
    if (session.ShouldTakeOverPrimary(primary_session_.get())) {
      // Push primary session to sessions list.
      sessions_.push_back(std::move(primary_session_));
      // Find the session in the list and promote it to primary.
      primary_session_ = sessions_.erase(session);
      ZX_ASSERT(primary_session_);
      // Notify rx queue of primary session change.
      rx_queue_->TriggerSessionChanged();
    }
    should_start = active_primary_sessions_ != 0;
  }

  if (should_start) {
    // Start the device if we haven't done so already.
    // NB: StartDeviceLocked releases the control lock.
    StartDeviceLocked();
  } else {
    control_lock_.Release();
  }

  if (evt_session_started_) {
    evt_session_started_(session.name());
  }
}

bool DeviceInterface::SessionStoppedInner(Session& session) {
  if (session.IsListen()) {
    bool any = primary_session_ && primary_session_->IsListen() && !primary_session_->IsPaused();
    for (auto& s : sessions_) {
      any |= s.IsListen() && !s.IsPaused();
    }
    has_listen_sessions_.store(any, std::memory_order_relaxed);
  }

  if (!session.IsPrimary()) {
    return false;
  }

  ZX_ASSERT(active_primary_sessions_ > 0);
  if (&session == primary_session_.get()) {
    // If this was the primary session, offer all other sessions to take over:
    Session* primary_candidate = &session;
    for (auto& i : sessions_) {
      primary_candidate->AssertParentControlLockShared(*this);
      if (primary_candidate->IsDying() || i.ShouldTakeOverPrimary(primary_candidate)) {
        primary_candidate = &i;
      }
    }
    // If we found a candidate to take over primary...
    if (primary_candidate != primary_session_.get()) {
      // ...promote it.
      sessions_.push_back(std::move(primary_session_));
      primary_session_ = sessions_.erase(*primary_candidate);
      ZX_ASSERT(primary_session_);
    }
    if (teardown_state_ == TeardownState::RUNNING) {
      rx_queue_->TriggerSessionChanged();
    }
  }

  active_primary_sessions_--;
  return active_primary_sessions_ == 0;
}

void DeviceInterface::SessionStopped(Session& session) {
  if (SessionStoppedInner(session)) {
    // Stop the device, no more sessions are running.
    StopDevice();
  } else {
    control_lock_.Release();
  }
}

void DeviceInterface::StartDevice() {
  LOGF_TRACE("%s", __FUNCTION__);
  control_lock_.Acquire();
  StartDeviceLocked();
}

void DeviceInterface::StartDeviceLocked() {
  LOGF_TRACE("%s", __FUNCTION__);

  bool start = false;
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

  control_lock_.Release();
  if (start) {
    StartDeviceInner();
  }
}

void DeviceInterface::StartDeviceInner() {
  LOGF_TRACE("%s", __FUNCTION__);
  device_.Start(
      [](void* cookie, zx_status_t status) {
        auto device = reinterpret_cast<DeviceInterface*>(cookie);
        {
          fbl::AutoLock lock(&device->control_lock_);
          ZX_ASSERT_MSG(device->device_status_ == DeviceStatus::STARTING,
                        "device not in starting status: %s",
                        DeviceStatusToString(device->device_status_));
          if (status != ZX_OK) {
            LOGF_ERROR("failed to start implementation: %s", zx_status_get_string(status));
            switch (device->SetDeviceStatus(DeviceStatus::STOPPED)) {
              case PendingDeviceOperation::STOP:
              case PendingDeviceOperation::NONE:
                break;
              case PendingDeviceOperation::START:
                ZX_PANIC("unexpected start pending while starting already");
                break;
            }
            if (device->primary_session_) {
              LOGF_ERROR("killing session '%s' because device failed to start",
                         device->primary_session_->name());
              device->primary_session_->Kill();
            }
            for (auto& s : device->sessions_) {
              LOGF_ERROR("killing session '%s' because device failed to start", s.name());
              s.Kill();
            }
            // We have effectively shut down the device, so finish tearing it down.
            device->ContinueTeardown(TeardownState::SESSIONS);
            return;
          }
        }
        device->DeviceStarted();
      },
      this);
}

void DeviceInterface::StopDevice(std::optional<TeardownState> continue_teardown) {
  LOGF_TRACE("%s", __FUNCTION__);
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
  if (continue_teardown.has_value()) {
    bool did_teardown = ContinueTeardown(continue_teardown.value());
    stop = stop && !did_teardown;
  } else {
    control_lock_.Release();
  }
  if (stop) {
    StopDeviceInner();
  }
}

void DeviceInterface::StopDeviceInner() {
  LOGF_TRACE("%s", __FUNCTION__);
  device_.Stop([](void* cookie) { reinterpret_cast<DeviceInterface*>(cookie)->DeviceStopped(); },
               this);
}

PendingDeviceOperation DeviceInterface::SetDeviceStatus(DeviceStatus status) {
  PendingDeviceOperation pending_op = pending_device_op_;
  device_status_ = status;
  pending_device_op_ = PendingDeviceOperation::NONE;
  return pending_op;
}

void DeviceInterface::DeviceStarted() {
  LOGF_TRACE("%s", __FUNCTION__);
  control_lock_.Acquire();
  switch (SetDeviceStatus(DeviceStatus::STARTED)) {
    case PendingDeviceOperation::STOP:
      StopDevice();
      return;
    case PendingDeviceOperation::NONE:
    case PendingDeviceOperation::START:
      break;
  }
  NotifyTxQueueAvailable();
  control_lock_.Release();
  // Notify Rx queue that the device has started.
  rx_queue_->TriggerRxWatch();
}

void DeviceInterface::DeviceStopped() {
  LOGF_TRACE("%s", __FUNCTION__);
  control_lock_.Acquire();
  PendingDeviceOperation pending_op = SetDeviceStatus(DeviceStatus::STOPPED);
  if (ContinueTeardown(TeardownState::SESSIONS)) {
    return;
  }
  switch (pending_op) {
    case PendingDeviceOperation::START:
      StartDevice();
      return;
    case PendingDeviceOperation::NONE:
    case PendingDeviceOperation::STOP:
      break;
  }
}

bool DeviceInterface::ContinueTeardown(network::internal::DeviceInterface::TeardownState state) {
  // The teardown process goes through different phases, encoded by the TeardownState enumeration.
  // - RUNNING: no teardown is in process. We move out of the RUNNING state by calling Unbind on all
  // the DeviceInterface's bindings.
  // - BINDINGS: Waiting for all bindings to close. Only moves to next state once all bindings are
  // closed, then calls unbind on all watchers and moves to the WATCHERS state.
  // - PORTS: Waiting for all ports to teardown. Only moves to the next state once all ports are
  // destroyed, then proceeds to stop and destroy all sessions.
  // - SESSIONS: Waiting for all sessions to be closed and destroyed (dead or alive). This is the
  // final stage, once all the sessions are properly destroyed the teardown_callback_ will be
  // triggered, marking the end of the teardown process.
  //
  // To protect the linearity of the teardown process, once it has started (the state is no longer
  // RUNNING) no more bindings, watchers, or sessions can be created.

  fit::callback<void()> teardown_callback =
      [this, state]() __TA_REQUIRES(control_lock_) -> fit::callback<void()> {
    if (state != teardown_state_) {
      return nullptr;
    }
    switch (teardown_state_) {
      case TeardownState::RUNNING: {
        teardown_state_ = TeardownState::BINDINGS;
        LOGF_TRACE("teardown state is BINDINGS (%ld bindings to destroy)", bindings_.size());
        if (!bindings_.is_empty()) {
          for (auto& b : bindings_) {
            b.Unbind();
          }
        }
        __FALLTHROUGH;
      }
      case TeardownState::BINDINGS: {
        // Pre-condition to enter port watchers state: bindings must be empty.
        if (!bindings_.is_empty()) {
          return nullptr;
        }
        teardown_state_ = TeardownState::PORT_WATCHERS;
        LOGF_TRACE("teardown state is PORT_WATCHERS (%ld watchers to destroy)",
                   port_watchers_.size());
        if (!port_watchers_.is_empty()) {
          for (auto& w : port_watchers_) {
            w.Unbind();
          }
        }
        __FALLTHROUGH;
      }
      case TeardownState::PORT_WATCHERS: {
        // Pre-condition to enter ports state: port watchers must be empty.
        if (!port_watchers_.is_empty()) {
          return nullptr;
        }
        teardown_state_ = TeardownState::PORTS;
        size_t port_count = 0;
        for (auto& p : ports_) {
          if (p.port) {
            p.port->Teardown();
            port_count++;
          }
        }
        LOGF_TRACE("teardown state is PORTS (%ld ports to destroy)", port_count);
        __FALLTHROUGH;
      }
      case TeardownState::PORTS: {
        // Pre-condition to enter sessions state: ports must all be destroyed.
        if (std::any_of(ports_.begin(), ports_.end(),
                        [](const PortSlot& port) { return static_cast<bool>(port.port); })) {
          return nullptr;
        }
        teardown_state_ = TeardownState::SESSIONS;
        LOGF_TRACE("teardown state is SESSIONS (primary=%s) (alive=%ld) (dead=%ld)",
                   primary_session_ ? "true" : "false", sessions_.size(), dead_sessions_.size());
        if (primary_session_ || !sessions_.is_empty()) {
          // If we have any sessions, signal all of them to stop their threads callback. Each
          // session that finishes operating will go through the `NotifyDeadSession` machinery. The
          // teardown is only complete when all sessions are destroyed.
          LOG_TRACE("teardown: sessions are running, scheduling teardown");
          if (primary_session_) {
            primary_session_->Kill();
          }
          for (auto& s : sessions_) {
            s.Kill();
          }
          // We won't check for dead sessions here, since all the sessions we just called `Kill` on
          // will go into the dead state asynchronously. Any sessions that are already in the dead
          // state will also get checked in `PruneDeadSessions` at a later time.
          return nullptr;
        }
        // No sessions are alive. Now check if we have any dead sessions that are waiting to reclaim
        // buffers.
        if (!dead_sessions_.is_empty()) {
          LOG_TRACE("teardown: dead sessions pending, waiting for teardown");
          // We need to wait for the device to safely give us all the buffers back before completing
          // the teardown.
          return nullptr;
        }
        // We can teardown immediately, let it fall through
        __FALLTHROUGH;
      }
      case TeardownState::SESSIONS: {
        // Condition to finish teardown: no more sessions exists (dead or alive) and the device
        // state is STOPPED.
        if (sessions_.is_empty() && !primary_session_ && dead_sessions_.is_empty() &&
            device_status_ == DeviceStatus::STOPPED) {
          teardown_state_ = TeardownState::FINISHED;
          LOG_TRACE("teardown finished");
          return std::move(teardown_callback_);
        }
        LOG_TRACE("teardown: Still pending sessions teardown");
        return nullptr;
      }
      case TeardownState::FINISHED:
        ZX_PANIC("nothing to do if the teardown state is finished.");
    }
  }();
  control_lock_.Release();
  if (teardown_callback) {
    teardown_callback();
    return true;
  }
  return false;
}

void DeviceInterface::NotifyPortRxFrame(uint8_t base_id, uint64_t frame_length) {
  WithPort(base_id, [&frame_length](const std::unique_ptr<DevicePort>& port) {
    if (port) {
      DevicePort::Counters& counters = port->counters();
      counters.rx_frames.fetch_add(1);
      counters.rx_bytes.fetch_add(frame_length);
    }
  });
}

zx::result<AttachedPort> DeviceInterface::AcquirePort(
    netdev::wire::PortId port_id, cpp20::span<const netdev::wire::FrameType> rx_frame_types) {
  return WithPort(port_id.base,
                  [this, &rx_frame_types, salt = port_id.salt](
                      const std::unique_ptr<DevicePort>& port) -> zx::result<AttachedPort> {
                    if (port == nullptr || port->id().salt != salt) {
                      return zx::error(ZX_ERR_NOT_FOUND);
                    }
                    if (std::any_of(rx_frame_types.begin(), rx_frame_types.end(),
                                    [&port](netdev::wire::FrameType frame_type) {
                                      return !port->IsValidRxFrameType(frame_type);
                                    })) {
                      return zx::error(ZX_ERR_INVALID_ARGS);
                    }
                    return zx::ok(AttachedPort(this, port.get(), rx_frame_types));
                  });
}

void DeviceInterface::OnPortTeardownComplete(DevicePort& port) {
  LOGF_TRACE("%s(%d)", __FUNCTION__, port.id().base);

  control_lock_.Acquire();
  bool stop_device = false;
  // Go over the non-primary sessions first, so we don't mess with the primary session.
  for (auto& session : sessions_) {
    session.AssertParentControlLock(*this);
    if (session.OnPortDestroyed(port.id().base)) {
      stop_device |= SessionStoppedInner(session);
    }
  }
  if (primary_session_) {
    primary_session_->AssertParentControlLock(*this);
    if (primary_session_->OnPortDestroyed(port.id().base)) {
      stop_device |= SessionStoppedInner(*primary_session_);
    }
  }
  ports_[port.id().base].port = nullptr;
  if (stop_device) {
    StopDevice(TeardownState::PORTS);
  } else {
    ContinueTeardown(TeardownState::PORTS);
  }
}

void DeviceInterface::ReleaseVmo(Session& session) {
  uint8_t vmo;
  vmo = session.ClearDataVmo();
  zx::result result = vmo_store_.Unregister(vmo);
  if (result.is_error()) {
    // Avoid notifying the device implementation if unregistration fails.
    // A non-ok return here means we're either attempting to double-release a VMO or the sessions
    // didn't have a registered VMO.
    LOGF_WARN("%s: Failed to unregister VMO %d: %s", session.name(), vmo, result.status_string());
    return;
  }

  // NB: We're calling into the device layer with the control lock held here.
  device_.ReleaseVmo(vmo);
}

fbl::RefPtr<RefCountedFifo> DeviceInterface::primary_rx_fifo() {
  SharedAutoLock lock(&control_lock_);
  if (primary_session_) {
    return primary_session_->rx_fifo();
  }
  return nullptr;
}

void DeviceInterface::NotifyTxQueueAvailable() { tx_queue_->Resume(); }

void DeviceInterface::NotifyTxReturned(bool was_full) {
  SharedAutoLock lock(&control_lock_);
  if (was_full) {
    NotifyTxQueueAvailable();
  }
  PruneDeadSessions();
}

void DeviceInterface::QueueRxSpace(const rx_space_buffer_t* rx, size_t count) {
  LOGF_TRACE("%s(_, %ld)", __FUNCTION__, count);
  device_.QueueRxSpace(rx, count);
}

void DeviceInterface::QueueTx(const tx_buffer_t* tx, size_t count) {
  LOGF_TRACE("%s(_, %ld)", __FUNCTION__, count);
  device_.QueueTx(tx, count);
}

void DeviceInterface::NotifyDeadSession(Session& dead_session) {
  LOGF_TRACE("%s('%s')", __FUNCTION__, dead_session.name());
  // First of all, stop all data-plane operations with stopped session.
  if (!dead_session.IsPaused()) {
    // Stop the session.
    // NB: SessionStopped releases the control lock.
    control_lock_.Acquire();
    SessionStopped(dead_session);
  }

  if (dead_session.IsPrimary()) {
    // Tell rx queue this session can't be used anymore.
    rx_queue_->PurgeSession(dead_session);
  }

  // Now find it in sessions and remove it.
  std::unique_ptr<Session> session_ptr;
  control_lock_.Acquire();
  if (&dead_session == primary_session_.get()) {
    // Nullify primary session.
    session_ptr = std::move(primary_session_);
    rx_queue_->TriggerSessionChanged();
  } else {
    session_ptr = sessions_.erase(dead_session);
  }

  // we can destroy the session immediately.
  if (session_ptr->ShouldDestroy()) {
    LOGF_TRACE("%s('%s') destroying session", __FUNCTION__, dead_session.name());
    ReleaseVmo(*session_ptr);
    session_ptr = nullptr;
    ContinueTeardown(TeardownState::SESSIONS);
    return;
  }

  // otherwise, add it to the list of dead sessions so we can wait for buffers to be returned before
  // destroying it.
  LOGF_TRACE(
      "%s('%s') session is dead, waiting for buffers to be "
      "reclaimed",
      __FUNCTION__, session_ptr->name());
  dead_sessions_.push_back(std::move(session_ptr));
  control_lock_.Release();
}

void DeviceInterface::PruneDeadSessions() __TA_REQUIRES_SHARED(control_lock_) {
  auto it = dead_sessions_.begin();
  while (it != dead_sessions_.end()) {
    Session& session = *it;
    // increment iterator before erasing, because of DoublyLinkedList
    ++it;
    if (session.ShouldDestroy()) {
      // Schedule for destruction.
      //
      // Destruction must happen later because we currently hold shared access to the control lock
      // and we need an exclusive lock to erase items from the dead sessions list.
      //
      // ShouldDestroy should only return true once in the lifetime of a session, which guarantees
      // that postponing the destruction on the dispatcher is always safe.
      async::PostTask(dispatcher_, [&session, this]() {
        control_lock_.Acquire();
        LOGF_TRACE("destroying %s", session.name());
        ReleaseVmo(session);
        dead_sessions_.erase(session);
        ContinueTeardown(TeardownState::SESSIONS);
      });
    } else {
      LOGF_TRACE("%s: %s still pending", __FUNCTION__, session.name());
    }
  }
}

void DeviceInterface::CommitAllSessions() {
  if (primary_session_) {
    primary_session_->AssertParentRxLock(*this);
    primary_session_->CommitRx();
  }
  for (auto& session : sessions_) {
    session.AssertParentRxLock(*this);
    session.CommitRx();
  }
  PruneDeadSessions();
}

void DeviceInterface::CopySessionData(const Session& owner, const RxFrameInfo& frame_info) {
  if (primary_session_ && primary_session_.get() != &owner) {
    primary_session_->AssertParentRxLock(*this);
    primary_session_->AssertParentControlLockShared(*this);
    primary_session_->CompleteRxWith(owner, frame_info);
  }

  for (auto& session : sessions_) {
    if (&session != &owner) {
      session.AssertParentRxLock(*this);
      session.AssertParentControlLockShared(*this);
      session.CompleteRxWith(owner, frame_info);
    }
  }
}

void DeviceInterface::ListenSessionData(const Session& owner,
                                        cpp20::span<const uint16_t> descriptors) {
  if ((device_info_.device_features & FEATURE_NO_AUTO_SNOOP) ||
      !has_listen_sessions_.load(std::memory_order_relaxed)) {
    // Avoid walking through sessions and acquiring Rx lock if we know no listen sessions are
    // attached.
    return;
  }
  fbl::AutoLock rx_lock(&rx_lock_);
  SharedAutoLock control(&control_lock_);
  bool copied = false;
  for (const uint16_t& descriptor : descriptors) {
    if (primary_session_ && primary_session_.get() != &owner && primary_session_->IsListen()) {
      primary_session_->AssertParentRxLock(*this);
      primary_session_->AssertParentControlLockShared(*this);
      copied |= primary_session_->ListenFromTx(owner, descriptor);
    }
    for (auto& s : sessions_) {
      if (&s != &owner && s.IsListen()) {
        s.AssertParentRxLock(*this);
        s.AssertParentControlLockShared(*this);
        copied |= s.ListenFromTx(owner, descriptor);
      }
    }
  }
  if (copied) {
    CommitAllSessions();
  }
}

zx_status_t DeviceInterface::LoadRxDescriptors(RxSessionTransaction& transact) {
  if (!primary_session_) {
    return ZX_ERR_BAD_STATE;
  }
  return primary_session_->LoadRxDescriptors(transact);
}

bool DeviceInterface::IsDataPlaneOpen() { return device_status_ == DeviceStatus::STARTED; }

DeviceInterface::DeviceInterface(async_dispatcher_t* dispatcher,
                                 ddk::NetworkDeviceImplProtocolClient parent)
    : dispatcher_(dispatcher),
      device_(parent),
      vmo_store_(vmo_store::Options{
          vmo_store::MapOptions{ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_REQUIRE_NON_RESIZABLE,
                                nullptr},
          std::nullopt,
      }) {
  // Seed the port salts to some non-random but unpredictable value.
  union {
    uint8_t b[sizeof(uintptr_t)];
    uintptr_t ptr;
  } seed = {.ptr = reinterpret_cast<uintptr_t>(this)};
  for (size_t i = 0; i < ports_.size(); i++) {
    ports_[i].salt = static_cast<uint8_t>(i) ^ seed.b[i % sizeof(seed.b)];
  }
}  // namespace internal

zx_status_t DeviceInterface::Binding::Bind(DeviceInterface* interface,
                                           fidl::ServerEnd<netdev::Device> channel) {
  fbl::AllocChecker ac;
  std::unique_ptr<Binding> binding(new (&ac) Binding);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto* binding_ptr = binding.get();
  binding->binding_ =
      fidl::BindServer(interface->dispatcher_, std::move(channel), interface,
                       [binding_ptr](DeviceInterface* interface, fidl::UnbindInfo /*unused*/,
                                     fidl::ServerEnd<fuchsia_hardware_network::Device> /*unused*/) {
                         bool bindings_empty;
                         interface->control_lock_.Acquire();
                         interface->bindings_.erase(*binding_ptr);
                         bindings_empty = interface->bindings_.is_empty();
                         if (bindings_empty) {
                           interface->ContinueTeardown(TeardownState::BINDINGS);
                         } else {
                           interface->control_lock_.Release();
                         }
                       });
  interface->bindings_.push_front(std::move(binding));
  return ZX_OK;
}

void DeviceInterface::Binding::Unbind() {
  auto binding = std::move(binding_);
  if (binding.has_value()) {
    binding->Unbind();
  }
}

}  // namespace internal
}  // namespace network
