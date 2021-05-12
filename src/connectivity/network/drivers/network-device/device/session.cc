// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "session.h"

#include <fuchsia/hardware/network/llcpp/fidl.h>
#include <lib/fidl/epitaph.h>
#include <lib/fit/defer.h>
#include <zircon/device/network.h>

#include <fbl/alloc_checker.h>
#include <fbl/ref_counted.h>

#include "device_interface.h"
#include "log.h"
#include "tx_queue.h"

namespace network::internal {

constexpr uint32_t kKillTxKey = 0;
constexpr uint32_t kResumeTxKey = 1;
constexpr uint32_t kTxAvailKey = 2;

bool Session::IsListen() const {
  return static_cast<bool>(flags_ & netdev::wire::SessionFlags::kListenTx);
}

bool Session::IsPrimary() const {
  return static_cast<bool>(flags_ & netdev::wire::SessionFlags::kPrimary);
}

bool Session::IsPaused() const { return paused_; }

bool Session::ShouldTakeOverPrimary(const Session* current_primary) const {
  if ((!IsPrimary()) || current_primary == this) {
    // If we're not a primary session, or the primary is already ourselves, then we don't
    // want to take over.
    return false;
  }
  if (!current_primary) {
    // Always request to take over if there is no current primary session.
    return true;
  }
  if (current_primary->IsPaused() && !IsPaused()) {
    // If the current primary session is paused, but we aren't we can take it over.
    return true;
  }
  // Otherwise, the heuristic to apply here is that we want to use the
  // session that has the largest number of descriptors defined, as that relates to having more
  // buffers available for us.
  return descriptor_count_ > current_primary->descriptor_count_;
}

zx::status<std::pair<std::unique_ptr<Session>, netdev::wire::Fifos>> Session::Create(
    async_dispatcher_t* dispatcher, netdev::wire::SessionInfo& info, fidl::StringView name,
    DeviceInterface* parent, fidl::ServerEnd<netdev::Session> control) {
  if (info.descriptor_version != NETWORK_DEVICE_DESCRIPTOR_VERSION) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  fbl::AllocChecker ac;
  std::unique_ptr<Session> session(new (&ac) Session(dispatcher, info, name, parent));
  if (!ac.check()) {
    LOGF_ERROR("network-device: Failed to allocate session");
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  zx::status fifos = session->Init();
  if (fifos.is_error()) {
    LOGF_ERROR("network-device: Failed to init session %s: %s", session->name(),
               fifos.status_string());
    return fifos.take_error();
  }

  session->Bind(std::move(control));

  return zx::ok(std::make_pair(std::move(session), std::move(fifos.value())));
}

Session::Session(async_dispatcher_t* dispatcher, netdev::wire::SessionInfo& info,
                 fidl::StringView name, DeviceInterface* parent)
    : dispatcher_(dispatcher),
      name_([&name]() {
        std::remove_const<decltype(name_)>::type t;
        ZX_ASSERT(name.size() < t.size());
        char* end = std::copy(name.begin(), name.end(), t.begin());
        *end = '\0';
        return t;
      }()),
      vmo_descriptors_(std::move(info.descriptors)),
      paused_(true),
      descriptor_count_(info.descriptor_count),
      descriptor_length_(info.descriptor_length * sizeof(uint64_t)),
      flags_(info.options),
      frame_types_([&info]() {
        std::remove_const<decltype(frame_types_)>::type t;
        ZX_ASSERT(info.rx_frames.count() <= t.size());
        for (size_t i = 0; i < info.rx_frames.count(); i++) {
          t[i] = static_cast<uint8_t>(info.rx_frames[i]);
        }
        return t;
      }()),
      frame_type_count_(static_cast<uint32_t>(info.rx_frames.count())),
      parent_(parent) {
  // TODO(http://fxbug.dev/64310): We're storing requested frame types for now and using it on
  // SetPaused. This will not be necessary once session FIDL updates.
}

Session::~Session() {
  // Stop the Tx thread if it hasn't been stopped already. We need to do this on destruction in case
  // binding the control channel to the dispatcher fails.
  StopTxThread();
  ZX_ASSERT(in_flight_rx_ == 0);
  ZX_ASSERT(in_flight_tx_ == 0);
  ZX_ASSERT(vmo_id_ == MAX_VMOS);
  for (size_t i = 0; i < attached_ports_.size(); i++) {
    ZX_ASSERT_MSG(!attached_ports_[i].has_value(), "outstanding attached port %ld", i);
  }
  // attempts to send an epitaph, signaling that the buffers are reclaimed:
  if (control_channel_.has_value()) {
    fidl_epitaph_write(control_channel_->channel().get(), ZX_ERR_CANCELED);
  }

  LOGF_TRACE("network-device(%s): Session destroyed", name());
}

zx::status<netdev::wire::Fifos> Session::Init() {
  // Map the data and descriptors VMO:

  if (zx_status_t status = descriptors_.Map(
          vmo_descriptors_, 0, descriptor_count_ * descriptor_length_,
          ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_REQUIRE_NON_RESIZABLE, nullptr);
      status != ZX_OK) {
    LOGF_ERROR("network-device(%s): failed to map data VMO: %s", name(),
               zx_status_get_string(status));
    return zx::error(status);
  }

  // create the FIFOs
  fbl::AllocChecker ac;
  fifo_rx_ = fbl::MakeRefCountedChecked<RefCountedFifo>(&ac);
  if (!ac.check()) {
    LOGF_ERROR("network-device(%s): failed to allocate", name());
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  netdev::wire::Fifos fifos;
  if (zx_status_t status = zx::fifo::create(parent_->rx_fifo_depth(), sizeof(uint16_t), 0,
                                            &fifos.rx, &fifo_rx_->fifo);
      status != ZX_OK) {
    LOGF_ERROR("network-device(%s): failed to create rx FIFO", name());
    return zx::error(status);
  }
  if (zx_status_t status =
          zx::fifo::create(parent_->tx_fifo_depth(), sizeof(uint16_t), 0, &fifos.tx, &fifo_tx_);
      status != ZX_OK) {
    LOGF_ERROR("network-device(%s): failed to create tx FIFO", name());
    return zx::error(status);
  }
  if (zx_status_t status = zx::port::create(0, &tx_port_); status != ZX_OK) {
    LOGF_ERROR("network-device(%s): failed to create tx port", name());
    return zx::error(status);
  }

  {
    zx_status_t status = [this, &ac]() {
      // Lie about holding the parent receive lock. This is an initialization function we can't be
      // racing with anything.
      []() __TA_ASSERT(parent_->rx_lock()) {}();
      rx_return_queue_.reset(new (&ac) uint16_t[parent_->rx_fifo_depth()]);
      if (!ac.check()) {
        LOGF_ERROR("network-device(%s): failed to create return queue", name());
        ZX_ERR_NO_MEMORY;
      }
      rx_return_queue_count_ = 0;

      rx_avail_queue_.reset(new (&ac) uint16_t[parent_->rx_fifo_depth()]);
      if (!ac.check()) {
        LOGF_ERROR("network-device(%s): failed to create return queue", name());
        return ZX_ERR_NO_MEMORY;
      }
      rx_avail_queue_count_ = 0;
      return ZX_OK;
    }();
    if (status != ZX_OK) {
      return zx::error(status);
    }
  }

  thrd_t thread;
  if (thrd_create_with_name(
          &thread, [](void* ctx) { return reinterpret_cast<Session*>(ctx)->Thread(); },
          reinterpret_cast<void*>(this), "netdevice:session") != thrd_success) {
    LOGF_ERROR("network-device(%s): session failed to create thread", name());
    return zx::error(ZX_ERR_INTERNAL);
  }
  thread_ = thread;

  LOGF_TRACE(
      "network-device(%s): starting session:"
      " descriptor_count: %d,"
      " descriptor_length: %ld,"
      " flags: %08X",
      name(), descriptor_count_, descriptor_length_, static_cast<uint16_t>(flags_));

  return zx::ok(std::move(fifos));
}

void Session::Bind(fidl::ServerEnd<netdev::Session> channel) {
  binding_ = fidl::BindServer(dispatcher_, std::move(channel), this,
                              [](Session* self, fidl::UnbindInfo info,
                                 fidl::ServerEnd<fuchsia_hardware_network::Session> server_end) {
                                self->OnUnbind(info.reason(), std::move(server_end));
                              });
}

void Session::OnUnbind(fidl::Reason reason, fidl::ServerEnd<netdev::Session> channel) {
  LOGF_TRACE("network-device(%s): session unbound, reason=%d", name(), reason);

  // Stop the Tx thread immediately, so we stop fetching more tx buffers from the client.
  StopTxThread();

  // The session may linger around for a short while still if the device implementation is holding
  // on to buffers on the session's VMO. When the session is destroyed, it'll attempt to send an
  // epitaph message over the channel if it's still open. The Rx FIFO is not closed here since it's
  // possible it's currently shared with the Rx Queue. The session will drop its reference to the Rx
  // FIFO upon destruction.

  switch (reason) {
    case fidl::Reason::kUnbind:
    case fidl::Reason::kDispatcherError:
    case fidl::Reason::kTransportError:
    case fidl::Reason::kEncodeError:
    case fidl::Reason::kDecodeError:
    case fidl::Reason::kUnexpectedMessage:
      // Store the channel to send an epitaph once the session is destroyed.
      control_channel_ = std::move(channel);
      break;
    case fidl::Reason::kClose:
    case fidl::Reason::kPeerClosed:
      break;
  }

  // When the session is unbound we can just detach all the ports from it.
  {
    fbl::AutoLock lock(&parent_->control_lock());
    for (uint8_t i = 0; i < MAX_PORTS; i++) {
      // We can ignore the return from detaching, this port is about to get destroyed.
      zx::status<bool> __UNUSED result = DetachPortLocked(i);
    }
    dying_ = true;
  }

  // NOTE: the parent may destroy the session synchronously in NotifyDeadSession, this is the
  // last thing we can do safely with this session object.
  parent_->NotifyDeadSession(*this);
}

void Session::StopTxThread() {
  if (thread_.has_value()) {
    zx_port_packet_t packet;
    packet.type = ZX_PKT_TYPE_USER;
    packet.key = kKillTxKey;
    packet.status = ZX_OK;
    zx_status_t status = tx_port_.queue(&packet);
    if (status != ZX_OK) {
      LOGF_ERROR("network-device(%s): Failed to send kill tx signal: %s", name(),
                 zx_status_get_string(status));
    }
    thrd_join(*thread_, nullptr);
    thread_.reset();
  }
}

int Session::Thread() {
  zx_status_t result = [this]() {
    for (;;) {
      zx_port_packet_t packet;
      zx_status_t status = tx_port_.wait(zx::time::infinite(), &packet);
      if (status != ZX_OK) {
        LOGF_ERROR("network-device(%s): tx thread port wait failed: %s", name(),
                   zx_status_get_string(status));
        return status;
      }
      bool fifo_readable = false;
      bool watch_tx = false;
      switch (packet.key) {
        case kKillTxKey:
          return ZX_OK;
        case kResumeTxKey:
          watch_tx = true;
          break;
        case kTxAvailKey:
          if (packet.signal.observed & ZX_FIFO_PEER_CLOSED) {
            // Kill the session, tx FIFO was closed.
            return ZX_ERR_PEER_CLOSED;
          }
          if (packet.signal.observed & ZX_FIFO_READABLE) {
            fifo_readable = true;
          }
          break;
      }

      if (fifo_readable && !paused_.load()) {
        auto fetch_result = FetchTx();
        switch (fetch_result) {
          case ZX_OK:
          case ZX_ERR_SHOULD_WAIT:
            watch_tx = true;
            break;
          case ZX_ERR_IO_OVERRUN:
            // Don't set watch_tx to true, we need to wait for a new TxResume signal before waiting
            // on FIFO again, the device's Tx buffers are full.
            break;
          default:
            // Stop operating the tx FIFO and kill the session.
            return fetch_result;
        }
      }

      if (watch_tx) {
        status =
            fifo_tx_.wait_async(tx_port_, kTxAvailKey, ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED, 0);
        if (status != ZX_OK) {
          LOGF_ERROR("network-device(%s): Failed to install async wait for tx fifo: %s", name(),
                     zx_status_get_string(status));
          return status;
        }
      }
    }
  }();

  // Only kill the session if it wasn't a clean break from the loop.
  if (result != ZX_OK) {
    // Peer closed is an expected error, don't log it.
    if (result != ZX_ERR_PEER_CLOSED) {
      LOGF_ERROR("network-device(%s): TxThread finished with error %s", name(),
                 zx_status_get_string(result));
    }
    Kill();
  }

  return 0;
}

zx_status_t Session::FetchTx() {
  TxQueue::SessionTransaction transaction(&parent_->tx_queue(), this);

  if (transaction.overrun()) {
    return ZX_ERR_IO_OVERRUN;
  }
  ZX_ASSERT(transaction.available() <= kMaxFifoDepth);
  uint16_t fetch_buffer[kMaxFifoDepth];
  size_t read;
  if (zx_status_t status =
          fifo_tx_.read(sizeof(uint16_t), fetch_buffer, transaction.available(), &read);
      status != ZX_OK) {
    if (status != ZX_ERR_SHOULD_WAIT) {
      LOGF_TRACE("network-device(%s): tx fifo read failed %s", name(),
                 zx_status_get_string(status));
    }
    return status;
  }

  fbl::Span descriptors(fetch_buffer, read);
  // Let other sessions know of tx data.
  transaction.AssertParentTxLock(*parent_);
  parent_->ListenSessionData(*this, descriptors);

  uint32_t req_header_length = parent_->info().tx_head_length;
  uint32_t req_tail_length = parent_->info().tx_tail_length;

  uint32_t total_length = 0;
  SharedAutoLock lock(&parent_->control_lock());
  for (uint16_t desc_idx : descriptors) {
    auto* desc = descriptor(desc_idx);
    if (!desc) {
      LOGF_ERROR("network-device(%s): received out of bounds descriptor: %d", name(), desc_idx);
      return ZX_ERR_IO_INVALID;
    }

    if (desc->port_id >= attached_ports_.size()) {
      LOGF_ERROR("network-device(%s): received invalid tx port id: %d", name(), desc->port_id);
      return ZX_ERR_IO_INVALID;
    }
    std::optional<AttachedPort>& slot = attached_ports_[desc->port_id];
    if (!slot.has_value()) {
      // Port is not attached, immediately return the buffer with an error.
      // Tx on unattached port is not an unrecoverable error, especially because detaching a port
      // can race with regular tx.
      // This is not expected to be part of fast path operation, so it should be
      // fine to return one of these buffers at a time.
      desc->return_flags = static_cast<uint32_t>(netdev::wire::TxReturnFlags::kTxRetError |
                                                 netdev::wire::TxReturnFlags::kTxRetNotAvailable);
      zx_status_t status = fifo_tx_.write(sizeof(desc_idx), &desc_idx, 1, nullptr);
      switch (status) {
        case ZX_OK:
          break;
        case ZX_ERR_PEER_CLOSED:
          // Tx FIFO closing is an expected error.
          return ZX_ERR_PEER_CLOSED;
        default:
          LOGF_ERROR("network-device(%s): failed to return buffer with bad port number %d: %s",
                     name(), desc->port_id, zx_status_get_string(status));
          return ZX_ERR_IO_INVALID;
      }
      continue;
    }
    AttachedPort& port = slot.value();
    // Reject invalid tx types.
    port.AssertParentControlLockShared(*parent_);
    if (!port.WithPort([frame_type = desc->frame_type](DevicePort& p) {
          return p.IsValidRxFrameType(frame_type);
        })) {
      return ZX_ERR_IO_INVALID;
    }

    auto* buffer = transaction.GetBuffer();
    buffer->meta.flags = desc->inbound_flags;
    buffer->meta.frame_type = desc->frame_type;
    buffer->meta.info_type = desc->info_type;
    if (buffer->meta.info_type != static_cast<uint32_t>(netdev::wire::InfoType::kNoInfo)) {
      LOGF_WARN("network-device(%s): Info type (%d) not recognized, discarding information", name(),
                buffer->meta.info_type);
    }

    buffer->data.vmo_id = vmo_id_;
    buffer->data.parts_count = 0;

    // check header space:
    if (desc->head_length < req_header_length) {
      LOGF_ERROR("network-device(%s): received buffer with insufficient head length: %d", name(),
                 desc->head_length);
      return ZX_ERR_IO_INVALID;
    }
    buffer->head_length = req_header_length;
    auto skip_front = desc->head_length - req_header_length;

    // check tail space:
    if (desc->tail_length < req_tail_length) {
      LOGF_ERROR("network-device(%s): received buffer with insufficient tail length: %d", name(),
                 desc->tail_length);
      return ZX_ERR_IO_INVALID;
    }
    buffer->tail_length = req_tail_length;

    // chain_length is the number of buffers to follow, so it must be strictly less than the maximum
    // descriptor chain value.
    if (desc->chain_length >= netdev::wire::kMaxDescriptorChain) {
      LOGF_ERROR("network-device(%s): received invalid chain length: %d", name(),
                 desc->chain_length);
      return ZX_ERR_IO_INVALID;
    }
    auto expect_chain = desc->chain_length;

    bool add_head_space = buffer->head_length != 0;
    for (;;) {
      auto* cur = const_cast<buffer_region_t*>(&buffer->data.parts_list[buffer->data.parts_count]);
      if (add_head_space) {
        cur->offset = desc->offset + skip_front;
        cur->length = desc->data_length + buffer->head_length;
      } else {
        cur->offset = desc->offset + desc->head_length;
        cur->length = desc->data_length;
      }
      if (expect_chain == 0 && buffer->tail_length) {
        cur->length += buffer->tail_length;
      }
      total_length += desc->data_length;
      buffer->data.parts_count++;

      add_head_space = false;
      if (expect_chain == 0) {
        break;
      }
      uint16_t didx = desc->nxt;
      desc = descriptor(didx);
      if (desc == nullptr) {
        LOGF_ERROR("network-device(%s): invalid chained descriptor index: %d", name(), didx);
        return ZX_ERR_IO_INVALID;
      }
      if (desc->chain_length != expect_chain - 1) {
        LOGF_ERROR("network-device(%s): invalid next chain length %d on descriptor %d", name(),
                   desc->chain_length, didx);
        return ZX_ERR_IO_INVALID;
      }
      expect_chain--;
    }

    if (total_length < parent_->info().min_tx_buffer_length) {
      LOGF_ERROR("network-device(%s): tx buffer length %d less than required minimum of %d", name(),
                 total_length, parent_->info().min_tx_buffer_length);
      return ZX_ERR_IO_INVALID;
    }
    transaction.Push(desc_idx);
  }

  return transaction.overrun() ? ZX_ERR_IO_OVERRUN : ZX_OK;
}

buffer_descriptor_t* Session::descriptor(uint16_t index) {
  if (index < descriptor_count_) {
    return reinterpret_cast<buffer_descriptor_t*>(static_cast<uint8_t*>(descriptors_.start()) +
                                                  (index * descriptor_length_));
  }
  return nullptr;
}

const buffer_descriptor_t* Session::descriptor(uint16_t index) const {
  if (index < descriptor_count_) {
    return reinterpret_cast<buffer_descriptor_t*>(static_cast<uint8_t*>(descriptors_.start()) +
                                                  (index * descriptor_length_));
  }
  return nullptr;
}

fbl::Span<uint8_t> Session::data_at(uint64_t offset, uint64_t len) const {
  auto mapped = data_vmo_->data();
  uint64_t max_len = mapped.size();
  offset = std::min(offset, max_len);
  len = std::min(len, max_len - offset);
  return mapped.subspan(offset, len);
}

void Session::ResumeTx() {
  zx_port_packet_t packet;
  packet.type = ZX_PKT_TYPE_USER;
  packet.key = kResumeTxKey;
  packet.status = ZX_OK;
  zx_status_t status = tx_port_.queue(&packet);
  if (status != ZX_OK) {
    LOGF_ERROR("network-device(%s): ResumeTx failed: %s", name(), zx_status_get_string(status));
  }
}

void Session::SetPaused(SetPausedRequestView request, SetPausedCompleter::Sync& _completer) {
  // TODO(http://fxbug.dev/64310): Transitionally setting a session as paused means attaching or
  // detaching port0, until we migrate the session FIDL.
  zx_status_t status;
  if (request->paused) {
    status = DetachPort(DeviceInterface::kPort0);
  } else {
    status = AttachPort(DeviceInterface::kPort0, fbl::Span(frame_types_.data(), frame_type_count_));
  }
  if (status != ZX_OK) {
    LOGF_WARN("network-device(%s): SetPaused(%d): %s", name(), request->paused,
              zx_status_get_string(status));
  }
}

zx_status_t Session::AttachPort(uint8_t port_id, fbl::Span<const uint8_t> frame_types) {
  size_t attached_count;
  {
    fbl::AutoLock lock(&parent_->control_lock());

    if (port_id >= attached_ports_.size()) {
      return ZX_ERR_INVALID_ARGS;
    }
    std::optional<AttachedPort>& slot = attached_ports_[port_id];
    if (slot.has_value()) {
      return ZX_ERR_ALREADY_EXISTS;
    }

    zx::status<AttachedPort> acquire_port = parent_->AcquirePort(port_id, frame_types);
    if (acquire_port.is_error()) {
      return acquire_port.status_value();
    }
    AttachedPort& port = acquire_port.value();
    port.AssertParentControlLockShared(*parent_);
    port.WithPort([](DevicePort& p) { p.SessionAttached(); });
    slot = port;

    // Count how many ports we have attached now so we know if we need to notify the parent of
    // changes to our state.
    attached_count =
        std::count_if(attached_ports_.begin(), attached_ports_.end(),
                      [](const std::optional<AttachedPort>& p) { return p.has_value(); });
  }
  // The newly attached port is the only port we're attached to, notify the parent that we want to
  // start up and kick the tx thread.
  if (attached_count == 1) {
    paused_.store(false);
    parent_->SessionStarted(*this);
    ResumeTx();
  }

  return ZX_OK;
}

zx_status_t Session::DetachPort(uint8_t port_id) {
  bool stop_session;
  {
    fbl::AutoLock lock(&parent_->control_lock());
    auto result = DetachPortLocked(port_id);
    if (result.is_error()) {
      return result.error_value();
    }
    stop_session = result.value();
  }
  // The newly detached port was the last one standing, notify parent we're a stopped session now.
  if (stop_session) {
    paused_.store(true);
    parent_->SessionStopped(*this);
  }
  return ZX_OK;
}

zx::status<bool> Session::DetachPortLocked(uint8_t port_id) {
  if (port_id >= attached_ports_.size()) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  std::optional<AttachedPort>& slot = attached_ports_[port_id];
  if (!slot.has_value()) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  AttachedPort& attached_port = slot.value();
  attached_port.AssertParentControlLockShared(*parent_);
  attached_port.WithPort([](DevicePort& p) { p.SessionDetached(); });
  slot = std::nullopt;
  return zx::ok(
      std::all_of(attached_ports_.begin(), attached_ports_.end(),
                  [](const std::optional<AttachedPort>& port) { return !port.has_value(); }));
}

bool Session::OnPortDestroyed(uint8_t port_id) {
  zx::status status = DetachPortLocked(port_id);
  // Tolerate errors on port destruction, just means we weren't attached to this port.
  if (status.is_error()) {
    return false;
  }
  bool should_stop = status.value();
  if (should_stop) {
    paused_ = true;
  }
  return should_stop;
}

void Session::Close(CloseRequestView request, CloseCompleter::Sync& _completer) { Kill(); }

void Session::MarkTxReturnResult(uint16_t descriptor_index, zx_status_t status) {
  ZX_ASSERT(descriptor_index < descriptor_count_);
  auto* desc = descriptor(descriptor_index);
  using netdev::wire::TxReturnFlags;
  switch (status) {
    case ZX_OK:
      desc->return_flags = 0;
      break;
    case ZX_ERR_NOT_SUPPORTED:
      desc->return_flags =
          static_cast<uint32_t>(TxReturnFlags::kTxRetNotSupported | TxReturnFlags::kTxRetError);
      break;
    case ZX_ERR_NO_RESOURCES:
      desc->return_flags =
          static_cast<uint32_t>(TxReturnFlags::kTxRetOutOfResources | TxReturnFlags::kTxRetError);
      break;
    case ZX_ERR_UNAVAILABLE:
      desc->return_flags =
          static_cast<uint32_t>(TxReturnFlags::kTxRetNotAvailable | TxReturnFlags::kTxRetError);
      break;
    case ZX_ERR_INTERNAL:
      // ZX_ERR_INTERNAL should never assume any flag semantics besides generic error.
      __FALLTHROUGH;
    default:
      desc->return_flags = static_cast<uint32_t>(TxReturnFlags::kTxRetError);
      break;
  }
}

void Session::ReturnTxDescriptors(const uint16_t* descriptors, size_t count) {
  size_t actual_count;
  zx_status_t status = fifo_tx_.write(sizeof(uint16_t), descriptors, count, &actual_count);
  constexpr char kLogFormat[] = "network-device(%s): failed to return %ld tx descriptors: %s";
  switch (status) {
    case ZX_OK:
      if (actual_count != count) {
        LOGF_ERROR("network-device(%s): failed to return %ld/%ld tx descriptors", name(),
                   count - actual_count, count);
      }
      break;
    case ZX_ERR_PEER_CLOSED:
      LOGF_WARN(kLogFormat, name(), count, zx_status_get_string(status));
      break;
    default:
      LOGF_ERROR(kLogFormat, name(), count, zx_status_get_string(status));
      break;
  }
  // Always assume we were able to return the descriptors.
  // After descriptors are marked as returned, the session may be destroyed.
  TxReturned(count);
}

bool Session::LoadAvailableRxDescriptors(RxQueue::SessionTransaction& transact) {
  transact.AssertLock(*parent_);
  LOGF_TRACE("network-device(%s): %s available:%ld transaction:%d", name(), __FUNCTION__,
             rx_avail_queue_count_, transact.remaining());
  if (rx_avail_queue_count_ == 0) {
    return false;
  }
  while (transact.remaining() != 0 && rx_avail_queue_count_ != 0) {
    rx_avail_queue_count_--;
    transact.Push(this, rx_avail_queue_[rx_avail_queue_count_]);
  }
  return true;
}

zx_status_t Session::FetchRxDescriptors() {
  ZX_ASSERT(rx_avail_queue_count_ == 0);
  if (!rx_valid_) {
    // This session is being killed and the rx path is not valid anymore.
    return ZX_ERR_BAD_STATE;
  }
  zx_status_t status;
  if ((status = fifo_rx_->fifo.read(sizeof(uint16_t), rx_avail_queue_.get(),
                                    parent_->rx_fifo_depth(), &rx_avail_queue_count_)) != ZX_OK) {
    // TODO count ZX_ERR_SHOULD_WAITS here
    return status;
  }

  return ZX_OK;
}

zx_status_t Session::LoadRxDescriptors(RxQueue::SessionTransaction& transact) {
  transact.AssertLock(*parent_);
  if (rx_avail_queue_count_ == 0) {
    zx_status_t status = FetchRxDescriptors();
    if (status != ZX_OK) {
      return status;
    }
  } else if (!rx_valid_) {
    return ZX_ERR_BAD_STATE;
  }
  // If we get here, we either have available descriptors or fetching more descriptors succeeded.
  // Loading from the available pool must succeed.
  ZX_ASSERT(LoadAvailableRxDescriptors(transact));
  return ZX_OK;
}

void Session::Kill() {
  if (binding_.has_value()) {
    binding_->Unbind();
  }
}

zx_status_t Session::FillRxSpace(uint16_t descriptor_index, rx_space_buffer_t* buff) {
  auto* desc = descriptor(descriptor_index);
  ZX_ASSERT(desc != nullptr);
  ZX_ASSERT(buff->data.parts_list != nullptr);

  // chain_length is the number of buffers to follow, so it must be strictly less than the maximum
  // descriptor chain value.
  if (desc->chain_length >= netdev::wire::kMaxDescriptorChain) {
    LOGF_ERROR("network-device(%s): received invalid chain length: %d", name(), desc->chain_length);
    return ZX_ERR_INVALID_ARGS;
  }
  buff->data.parts_count = 0;

  // We need the const cast here to go around banjo code generation. It looks very ugly, though.
  auto* buffer_parts = const_cast<buffer_region_t*>(buff->data.parts_list);
  buff->data.vmo_id = vmo_id_;

  auto expect_chain = desc->chain_length;
  uint32_t total_length = 0;
  for (;;) {
    buffer_parts->offset = desc->offset + desc->head_length;
    buffer_parts->length = desc->data_length;
    buff->data.parts_count++;
    total_length += desc->data_length;
    buffer_parts++;
    if (expect_chain == 0) {
      break;
    }
    uint16_t didx = desc->nxt;
    desc = descriptor(didx);
    if (desc == nullptr) {
      LOGF_ERROR("network-device(%s): invalid chained descriptor index: %d", name(), didx);
      return ZX_ERR_INVALID_ARGS;
    }
    if (desc->chain_length != expect_chain - 1) {
      LOGF_ERROR("network-device(%s): invalid next chain length %d on descriptor %d", name(),
                 desc->chain_length, didx);
      return ZX_ERR_INVALID_ARGS;
    }
    expect_chain--;
  }
  if (total_length < parent_->info().min_rx_buffer_length) {
    LOGF_ERROR("netwok-device(%s): rx buffer length %d less than required minimum of %d", name(),
               total_length, parent_->info().min_rx_buffer_length);
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

bool Session::CompleteRx(uint16_t descriptor_index, const rx_buffer_t* buff) {
  ZX_ASSERT(IsPrimary());

  // Always mark a single buffer as returned upon completion.
  auto defer = fit::defer([this]() {
    []() __TA_ASSERT(parent_->rx_lock()) {}();
    RxReturned();
  });

  if (buff->length != 0) {
    // Copy session data to other sessions (if any) even if this session is paused.
    parent_->CopySessionData(*this, descriptor_index, buff);

    if (IsSubscribedToFrameType(buff->meta.port, buff->meta.frame_type) && !paused_.load()) {
      // We validated the descriptor coming in, writing it back should always work.
      ZX_ASSERT(LoadRxInfo(descriptor_index, buff) == ZX_OK);
      rx_return_queue_[rx_return_queue_count_++] = descriptor_index;
      // Never allow reusing a frame that has been put in the return queue.
      return false;
    }
  }

  // Allow the buffer to be immediately reused as long as our rx path is still valid.
  return rx_valid_;
}

void Session::CompleteRxWith(const Session& owner, uint16_t owner_index, const rx_buffer_t* buff) {
  // can't call this if owner is self.
  ZX_ASSERT(&owner != this);
  if (!IsSubscribedToFrameType(buff->meta.port, buff->meta.frame_type) || paused_.load()) {
    // don't do anything if we're paused or not subscribed to this frame type.
    return;
  }

  if (rx_avail_queue_count_ == 0) {
    // can't do much if we can't fetch more descriptors
    if (FetchRxDescriptors() != ZX_OK) {
      return;
    }
  }
  // shouldn't get here without available descriptors
  ZX_ASSERT(rx_avail_queue_count_ > 0);
  rx_avail_queue_count_--;
  auto target_desc = rx_avail_queue_[rx_avail_queue_count_];
  zx_status_t status = LoadRxInfo(target_desc, buff);
  if (status == ZX_ERR_NO_RESOURCES) {
    // buffer too long and didn't fit the descriptor. just return it to the queue.
    rx_avail_queue_[rx_avail_queue_count_] = target_desc;
    rx_avail_queue_count_++;
  } else if (status != ZX_OK) {
    // something else happened when loading the information, this session is feeding us invalid
    // descriptors. Just kill the session.
    Kill();
    return;
  }

  // copy the data from the owner VMO into our own.
  // we can assume that the owner descriptor is valid, because the descriptor was validated when
  // passing it down to the device.
  // Also, we know that our own descriptor is valid, because we already pre-loaded the information
  // by calling LoadRxInfo above.
  auto* owner_desc = owner.descriptor(owner_index);
  auto* desc = descriptor(target_desc);
  auto len = buff->length;
  uint32_t owner_off = 0;
  uint32_t self_off = 0;
  while (len > 0) {
    ZX_ASSERT(desc && owner_desc);
    auto owner_len = owner_desc->data_length - owner_off;
    auto self_len = desc->data_length - self_off;
    auto copy_len = owner_len >= self_len ? self_len : owner_len;
    auto target = data_at(desc->offset + desc->head_length + self_off, copy_len);
    auto src = owner.data_at(owner_desc->offset + owner_desc->head_length + owner_off, copy_len);
    std::copy_n(src.begin(), std::min(target.size(), src.size()), target.begin());

    owner_off += copy_len;
    self_off += copy_len;
    ZX_ASSERT(owner_off <= owner_desc->data_length);
    ZX_ASSERT(self_off <= desc->data_length);
    if (self_off == desc->data_length) {
      desc = descriptor(desc->nxt);
      self_off = 0;
    }
    if (owner_off == owner_desc->data_length) {
      owner_desc = owner.descriptor(owner_desc->nxt);
      owner_off = 0;
    }
    len -= copy_len;
  }

  // add the descriptor to the return queue.
  rx_return_queue_[rx_return_queue_count_] = target_desc;
  rx_return_queue_count_++;
}

bool Session::ListenFromTx(const Session& owner, uint16_t owner_index) {
  ZX_ASSERT(&owner != this);
  if (IsPaused()) {
    // Do nothing if we're paused.
    return false;
  }

  if (rx_avail_queue_count_ == 0) {
    // Can't do much if we can't fetch more descriptors.
    if (FetchRxDescriptors() != ZX_OK) {
      LOGF_TRACE("network-device(%s): Failed to fetch rx descriptors for Tx listening", name());
      return false;
    }
  }
  // Shouldn't get here without available descriptors.
  ZX_ASSERT(rx_avail_queue_count_ > 0);
  rx_avail_queue_count_--;
  auto target_desc = rx_avail_queue_[rx_avail_queue_count_];

  const buffer_descriptor_t* owner_desc = owner.descriptor(owner_index);
  buffer_descriptor_t* desc = descriptor(target_desc);
  // NOTE(brunodalbo) Do we want to listen on info as well?
  desc->info_type = static_cast<uint32_t>(netdev::wire::InfoType::kNoInfo);
  desc->frame_type = owner_desc->frame_type;
  desc->return_flags = static_cast<uint32_t>(netdev::wire::RxFlags::kRxEchoedTx);

  uint64_t my_offset = 0;
  uint64_t owner_offset = 0;
  // Start copying the data over:
  while (owner_desc) {
    if (!desc) {
      // Not enough space to put data.
      break;
    }
    uint64_t me_avail = desc->data_length - my_offset;
    uint64_t owner_avail = owner_desc->data_length - owner_offset;
    uint64_t copy = me_avail < owner_avail ? me_avail : owner_avail;

    fbl::Span target = data_at(desc->offset + desc->head_length + my_offset, copy);
    fbl::Span src =
        owner.data_at(owner_desc->offset + owner_desc->head_length + owner_offset, copy);
    std::copy_n(src.begin(), std::min(target.size(), src.size()), target.begin());

    my_offset += copy;
    owner_offset += copy;
    if (my_offset == desc->data_length) {
      my_offset = 0;
      if (desc->chain_length != 0) {
        desc = descriptor(desc->nxt);
      } else {
        desc = nullptr;
      }
    }
    if (owner_offset == owner_desc->data_length) {
      owner_offset = 0;
      if (owner_desc->chain_length != 0) {
        owner_desc = owner.descriptor(owner_desc->nxt);
      } else {
        owner_desc = nullptr;
      }
    }
  }
  if (owner_desc) {
    LOGF_TRACE("network-device(%s): Failed to copy data from tx listen", name());
    // Did not reach end of data, just return descriptor to queue.
    rx_avail_queue_[rx_avail_queue_count_] = target_desc;
    rx_avail_queue_count_++;
    return false;
  }
  if (desc) {
    // Set length in last buffer.
    desc->data_length = static_cast<uint32_t>(my_offset);
  }

  // Add the descriptor to the return queue.
  rx_return_queue_[rx_return_queue_count_] = target_desc;
  rx_return_queue_count_++;

  return true;
}

zx_status_t Session::LoadRxInfo(uint16_t descriptor_index, const rx_buffer_t* buff) {
  auto* desc = descriptor(descriptor_index);
  if (desc == nullptr) {
    LOGF_ERROR("network-device(%s): invalid descriptor index %d", name(), descriptor_index);
    return ZX_ERR_INVALID_ARGS;
  }
  auto len = static_cast<uint32_t>(buff->length);

  if (buff->meta.info_type != static_cast<uint32_t>(netdev::wire::InfoType::kNoInfo)) {
    LOGF_WARN("network-device(%s): InfoType not recognized :%d", name(), buff->meta.info_type);
  } else {
    desc->info_type = static_cast<uint32_t>(netdev::wire::InfoType::kNoInfo);
  }
  desc->frame_type = buff->meta.frame_type;
  desc->inbound_flags = buff->meta.flags;
  desc->port_id = buff->meta.port;
  if (desc->chain_length >= netdev::wire::kMaxDescriptorChain) {
    LOGF_ERROR("network-device(%s): invalid descriptor %d chain length %d", name(),
               descriptor_index, desc->chain_length);
    return ZX_ERR_INVALID_ARGS;
  }
  auto didx = descriptor_index;
  while (len > 0) {
    if (desc->data_length >= len) {
      desc->data_length = len;
      len = 0;
    } else {
      len -= desc->data_length;
      if (desc->chain_length == 0) {
        LOGF_ERROR("network-device(%s): can't fill entire length in descriptor %d => %d: %d",
                   name(), descriptor_index, didx, len);
        return ZX_ERR_NO_RESOURCES;
      }
      didx = desc->nxt;
      desc = descriptor(didx);
      if (desc == nullptr) {
        LOGF_ERROR("network-device(%s): invalid descriptor chain %d => %d", name(),
                   descriptor_index, didx);
        return ZX_ERR_INVALID_ARGS;
      }
    }
  }

  // zero out the data of any final chained parts
  while (desc->chain_length != 0) {
    desc = descriptor(desc->nxt);
    desc->data_length = 0;
  }

  return ZX_OK;
}

void Session::CommitRx() {
  if (rx_return_queue_count_ == 0 || paused_.load()) {
    return;
  }
  size_t actual;
  zx_status_t status = fifo_rx_->fifo.write(sizeof(uint16_t), rx_return_queue_.get(),
                                            rx_return_queue_count_, &actual);
  constexpr char kLogFormat[] = "network-device(%s): failed to return %ld rx descriptors: %s";
  switch (status) {
    case ZX_OK:
      if (actual != rx_return_queue_count_) {
        LOGF_ERROR("network-device(%s): failed to return %ld/%ld rx descriptors", name(),
                   rx_return_queue_count_ - actual, rx_return_queue_count_);
      }
      break;
    case ZX_ERR_PEER_CLOSED:
      LOGF_WARN(kLogFormat, name(), rx_return_queue_count_, zx_status_get_string(status));
      break;

    default:
      LOGF_ERROR(kLogFormat, name(), rx_return_queue_count_, zx_status_get_string(status));
      break;
  }
  // Always assume we were able to return the descriptors.
  rx_return_queue_count_ = 0;
}

bool Session::IsSubscribedToFrameType(uint8_t port, uint8_t frame_type) {
  if (port >= attached_ports_.size()) {
    return false;
  }
  std::optional<AttachedPort>& slot = attached_ports_[port];
  if (!slot.has_value()) {
    return false;
  }
  fbl::Span subscribed = slot.value().frame_types();
  return std::any_of(subscribed.begin(), subscribed.end(),
                     [frame_type](const uint8_t& t) { return t == frame_type; });
}

void Session::SetDataVmo(uint8_t vmo_id, DataVmoStore::StoredVmo* vmo) {
  ZX_ASSERT_MSG(vmo_id_ == MAX_VMOS, "data VMO already set");
  ZX_ASSERT_MSG(vmo_id < MAX_VMOS, "invalid vmo_id %d", vmo_id);
  vmo_id_ = vmo_id;
  data_vmo_ = vmo;
}

uint8_t Session::ClearDataVmo() {
  uint8_t id = vmo_id_;
  // Reset identifier to the marker value. The destructor will assert that `ReleaseDataVmo` was
  // called by checking the value.
  vmo_id_ = MAX_VMOS;
  data_vmo_ = nullptr;
  return id;
}

}  // namespace network::internal
