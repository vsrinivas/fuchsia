// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "session.h"

#include <fidl/fuchsia.hardware.network/cpp/wire.h>
#include <lib/fidl/epitaph.h>
#include <lib/fit/defer.h>
#include <zircon/device/network.h>

#include <optional>

#include <fbl/alloc_checker.h>
#include <fbl/ref_counted.h>

#include "device_interface.h"
#include "lib/stdcompat/span.h"
#include "log.h"
#include "tx_queue.h"

namespace network::internal {

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

zx::result<std::pair<std::unique_ptr<Session>, netdev::wire::Fifos>> Session::Create(
    async_dispatcher_t* dispatcher, netdev::wire::SessionInfo& info, fidl::StringView name,
    DeviceInterface* parent, fidl::ServerEnd<netdev::Session> control) {
  // Validate required session fields.
  if (!(info.has_data() && info.has_descriptor_count() && info.has_descriptor_length() &&
        info.has_descriptor_version() && info.has_descriptors())) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  if (info.descriptor_version() != NETWORK_DEVICE_DESCRIPTOR_VERSION) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  fbl::AllocChecker ac;
  std::unique_ptr<Session> session(new (&ac) Session(dispatcher, info, name, parent));
  if (!ac.check()) {
    LOGF_ERROR("failed to allocate session");
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  zx::result fifos = session->Init();
  if (fifos.is_error()) {
    LOGF_ERROR("failed to init session %s: %s", session->name(), fifos.status_string());
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
      vmo_descriptors_(std::move(info.descriptors())),
      paused_(true),
      descriptor_count_(info.descriptor_count()),
      descriptor_length_(info.descriptor_length() * sizeof(uint64_t)),
      flags_(info.has_options() ? info.options() : netdev::wire::SessionFlags()),
      parent_(parent) {}

Session::~Session() {
  // Ensure session has removed itself from the tx queue.
  ZX_ASSERT(!tx_ticket_.has_value());
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

  LOGF_TRACE("%s: Session destroyed", name());
}

zx::result<netdev::wire::Fifos> Session::Init() {
  // Map the data and descriptors VMO:

  if (zx_status_t status = descriptors_.Map(
          vmo_descriptors_, 0, descriptor_count_ * descriptor_length_,
          ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_REQUIRE_NON_RESIZABLE, nullptr);
      status != ZX_OK) {
    LOGF_ERROR("%s: failed to map data VMO: %s", name(), zx_status_get_string(status));
    return zx::error(status);
  }

  // create the FIFOs
  fbl::AllocChecker ac;
  fifo_rx_ = fbl::MakeRefCountedChecked<RefCountedFifo>(&ac);
  if (!ac.check()) {
    LOGF_ERROR("%s: failed to allocate", name());
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  netdev::wire::Fifos fifos;
  if (zx_status_t status = zx::fifo::create(parent_->rx_fifo_depth(), sizeof(uint16_t), 0,
                                            &fifos.rx, &fifo_rx_->fifo);
      status != ZX_OK) {
    LOGF_ERROR("%s: failed to create rx FIFO", name());
    return zx::error(status);
  }
  if (zx_status_t status =
          zx::fifo::create(parent_->tx_fifo_depth(), sizeof(uint16_t), 0, &fifos.tx, &fifo_tx_);
      status != ZX_OK) {
    LOGF_ERROR("%s: failed to create tx FIFO", name());
    return zx::error(status);
  }

  {
    zx_status_t status = [this, &ac]() {
      // Lie about holding the parent receive lock. This is an initialization function we can't be
      // racing with anything.
      []() __TA_ASSERT(parent_->rx_lock()) {}();
      rx_return_queue_.reset(new (&ac) uint16_t[parent_->rx_fifo_depth()]);
      if (!ac.check()) {
        LOGF_ERROR("%s: failed to create return queue", name());
        ZX_ERR_NO_MEMORY;
      }
      rx_return_queue_count_ = 0;

      rx_avail_queue_.reset(new (&ac) uint16_t[parent_->rx_fifo_depth()]);
      if (!ac.check()) {
        LOGF_ERROR("%s: failed to create return queue", name());
        return ZX_ERR_NO_MEMORY;
      }
      rx_avail_queue_count_ = 0;
      return ZX_OK;
    }();
    if (status != ZX_OK) {
      return zx::error(status);
    }
  }

  LOGF_TRACE(
      "%s: starting session:"
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
                                self->OnUnbind(info, std::move(server_end));
                              });
}

void Session::OnUnbind(fidl::UnbindInfo info, fidl::ServerEnd<netdev::Session> channel) {
  LOGF_TRACE("%s: session unbound, info: %s", name(), info.FormatDescription().c_str());
  {
    fbl::AutoLock lock(&parent_->tx_lock());
    // Remove ourselves from the Tx thread worker so we stop fetching buffers
    // from the client.
    UninstallTx();
  }

  // The session may linger around for a short while still if the device
  // implementation is holding on to buffers on the session's VMO. When the
  // session is destroyed, it'll attempt to send an epitaph message over the
  // channel if it's still open. The Rx FIFO is not closed here since it's
  // possible it's currently shared with the Rx Queue. The session will drop its
  // reference to the Rx FIFO upon destruction.
  if (!info.is_peer_closed() && !info.did_send_epitaph()) {
    // Store the channel to send an epitaph once the session is destroyed.
    control_channel_ = std::move(channel);
  }

  {
    fbl::AutoLock lock(&parent_->control_lock());
    // When the session is unbound we can just detach all the ports from it.
    for (uint8_t i = 0; i < MAX_PORTS; i++) {
      // We can ignore the return from detaching, this port is about to get
      // destroyed.
      zx::result<bool> __UNUSED result = DetachPortLocked(i, std::nullopt);
    }
    dying_ = true;
  }

  // NOTE: the parent may destroy the session synchronously in
  // NotifyDeadSession, this is the last thing we can do safely with this
  // session object.
  parent_->NotifyDeadSession(*this);
}

void Session::InstallTx() {
  ZX_ASSERT(!tx_ticket_.has_value());
  TxQueue& tx_queue = parent_->tx_queue();
  tx_queue.AssertParentTxLocked(*parent_);
  tx_ticket_ = tx_queue.AddSession(this);
}

void Session::UninstallTx() {
  if (tx_ticket_.has_value()) {
    TxQueue& tx_queue = parent_->tx_queue();
    tx_queue.AssertParentTxLocked(*parent_);
    tx_queue.RemoveSession(std::exchange(tx_ticket_, std::nullopt).value());
  }
}

zx_status_t Session::FetchTx(TxQueue::SessionTransaction& transaction) {
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
      LOGF_TRACE("%s: tx fifo read failed %s", name(), zx_status_get_string(status));
    }
    return status;
  }

  cpp20::span descriptors(fetch_buffer, read);
  // Let other sessions know of tx data.
  transaction.AssertParentTxLock(*parent_);
  parent_->ListenSessionData(*this, descriptors);

  uint16_t req_header_length = parent_->info().tx_head_length;
  uint16_t req_tail_length = parent_->info().tx_tail_length;

  SharedAutoLock lock(&parent_->control_lock());
  for (uint16_t desc_idx : descriptors) {
    buffer_descriptor_t* const desc_ptr = checked_descriptor(desc_idx);
    if (!desc_ptr) {
      LOGF_ERROR("%s: received out of bounds descriptor: %d", name(), desc_idx);
      return ZX_ERR_IO_INVALID;
    }
    buffer_descriptor_t& desc = *desc_ptr;

    if (desc.port_id.base >= attached_ports_.size()) {
      LOGF_ERROR("%s: received invalid tx port id: %d", name(), desc.port_id.base);
      return ZX_ERR_IO_INVALID;
    }
    std::optional<AttachedPort>& slot = attached_ports_[desc.port_id.base];
    auto return_descriptor = [this, &desc, &desc_idx]() {
      // Tx on unattached port is a recoverable error; we must handle it gracefully because
      // detaching a port can race with regular tx.
      // This is not expected to be part of fast path operation, so it should be
      // fine to return one of these buffers at a time.
      desc.return_flags = static_cast<uint32_t>(netdev::wire::TxReturnFlags::kTxRetError |
                                                netdev::wire::TxReturnFlags::kTxRetNotAvailable);

      // TODO(https://fxbug.dev/32098): We're assuming that writing to the FIFO
      // here is a sufficient memory barrier for the other end to access the
      // data. That is currently true but not really guaranteed by the API.
      zx_status_t status = fifo_tx_.write(sizeof(desc_idx), &desc_idx, 1, nullptr);
      switch (status) {
        case ZX_OK:
          return ZX_OK;
        case ZX_ERR_PEER_CLOSED:
          // Tx FIFO closing is an expected error.
          return ZX_ERR_PEER_CLOSED;
        default:
          LOGF_ERROR("%s: failed to return buffer with bad port number %d: %s", name(),
                     desc.port_id.base, zx_status_get_string(status));
          return ZX_ERR_IO_INVALID;
      }
    };
    if (!slot.has_value()) {
      // Port is not attached, immediately return the descriptor with an error.
      if (zx_status_t status = return_descriptor(); status != ZX_OK) {
        return status;
      }
      continue;
    }
    AttachedPort& port = slot.value();
    // Reject invalid tx types.
    port.AssertParentControlLockShared(*parent_);
    if (!port.SaltMatches(desc.port_id.salt)) {
      // Bad port salt, immediately return the descriptor with an error.
      if (zx_status_t status = return_descriptor(); status != ZX_OK) {
        return status;
      }
      continue;
    }

    if (!port.WithPort([frame_type = desc.frame_type](DevicePort& p) {
          return p.IsValidRxFrameType(static_cast<netdev::wire::FrameType>(frame_type));
        })) {
      return ZX_ERR_IO_INVALID;
    }

    tx_buffer_t* buffer = transaction.GetBuffer();

    // check header space:
    if (desc.head_length < req_header_length) {
      LOGF_ERROR("%s: received buffer with insufficient head length: %d", name(), desc.head_length);
      return ZX_ERR_IO_INVALID;
    }
    auto skip_front = desc.head_length - req_header_length;

    // check tail space:
    if (desc.tail_length < req_tail_length) {
      LOGF_ERROR("%s: received buffer with insufficient tail length: %d", name(), desc.tail_length);
      return ZX_ERR_IO_INVALID;
    }

    auto info_type = static_cast<netdev::wire::InfoType>(desc.info_type);
    switch (info_type) {
      case netdev::wire::InfoType::kNoInfo:
        break;
      default:
        LOGF_ERROR("%s: info type (%d) not recognized, discarding information", name(),
                   buffer->meta.info_type);
        info_type = netdev::wire::InfoType::kNoInfo;
        break;
    }

    *buffer = {
        .data_list = buffer->data_list,
        .data_count = 0,
        .meta =
            {
                .port = desc.port_id.base,
                .info_type = static_cast<uint32_t>(info_type),
                .flags = desc.inbound_flags,
                .frame_type = desc.frame_type,
            },
        .head_length = req_header_length,
        .tail_length = req_tail_length,
    };

    // chain_length is the number of buffers to follow, so it must be strictly less than the maximum
    // descriptor chain value.
    if (desc.chain_length >= netdev::wire::kMaxDescriptorChain) {
      LOGF_ERROR("%s: received invalid chain length: %d", name(), desc.chain_length);
      return ZX_ERR_IO_INVALID;
    }
    auto expect_chain = desc.chain_length;

    bool add_head_space = buffer->head_length != 0;
    buffer_descriptor_t* part_iter = desc_ptr;
    uint32_t total_length = 0;
    for (;;) {
      buffer_descriptor_t& part_desc = *part_iter;
      auto* cur = const_cast<buffer_region_t*>(&buffer->data_list[buffer->data_count]);
      if (add_head_space) {
        *cur = {
            .vmo = vmo_id_,
            .offset = part_desc.offset + skip_front,
            .length = part_desc.data_length + buffer->head_length,
        };
      } else {
        *cur = {
            .vmo = vmo_id_,
            .offset = part_desc.offset + part_desc.head_length,
            .length = part_desc.data_length,
        };
      }
      if (expect_chain == 0 && buffer->tail_length) {
        cur->length += buffer->tail_length;
      }
      total_length += part_desc.data_length;
      buffer->data_count++;

      add_head_space = false;
      if (expect_chain == 0) {
        break;
      }
      uint16_t didx = part_desc.nxt;
      part_iter = checked_descriptor(didx);
      if (part_iter == nullptr) {
        LOGF_ERROR("%s: invalid chained descriptor index: %d", name(), didx);
        return ZX_ERR_IO_INVALID;
      }
      buffer_descriptor_t& next_desc = *part_iter;
      if (next_desc.chain_length != expect_chain - 1) {
        LOGF_ERROR("%s: invalid next chain length %d on descriptor %d", name(),
                   next_desc.chain_length, didx);
        return ZX_ERR_IO_INVALID;
      }
      expect_chain--;
    }

    if (total_length < parent_->info().min_tx_buffer_length) {
      LOGF_ERROR("%s: tx buffer length %d less than required minimum of %d", name(), total_length,
                 parent_->info().min_tx_buffer_length);
      return ZX_ERR_IO_INVALID;
    }

    port.WithPort([&total_length](DevicePort& p) {
      DevicePort::Counters& counters = p.counters();
      counters.tx_frames.fetch_add(1);
      counters.tx_bytes.fetch_add(total_length);
    });
    transaction.Push(desc_idx);
  }

  return transaction.overrun() ? ZX_ERR_IO_OVERRUN : ZX_OK;
}

buffer_descriptor_t* Session::checked_descriptor(uint16_t index) {
  if (index < descriptor_count_) {
    return reinterpret_cast<buffer_descriptor_t*>(static_cast<uint8_t*>(descriptors_.start()) +
                                                  (index * descriptor_length_));
  }
  return nullptr;
}

const buffer_descriptor_t* Session::checked_descriptor(uint16_t index) const {
  if (index < descriptor_count_) {
    return reinterpret_cast<buffer_descriptor_t*>(static_cast<uint8_t*>(descriptors_.start()) +
                                                  (index * descriptor_length_));
  }
  return nullptr;
}

buffer_descriptor_t& Session::descriptor(uint16_t index) {
  buffer_descriptor_t* desc = checked_descriptor(index);
  ZX_ASSERT_MSG(desc != nullptr, "descriptor %d out of bounds (%d)", index, descriptor_count_);
  return *desc;
}

const buffer_descriptor_t& Session::descriptor(uint16_t index) const {
  const buffer_descriptor_t* desc = checked_descriptor(index);
  ZX_ASSERT_MSG(desc != nullptr, "descriptor %d out of bounds (%d)", index, descriptor_count_);
  return *desc;
}

cpp20::span<uint8_t> Session::data_at(uint64_t offset, uint64_t len) const {
  auto mapped = data_vmo_->data();
  uint64_t max_len = mapped.size();
  offset = std::min(offset, max_len);
  len = std::min(len, max_len - offset);
  return mapped.subspan(offset, len);
}

zx_status_t Session::AttachPort(netdev::wire::PortId port_id,
                                cpp20::span<const netdev::wire::FrameType> frame_types) {
  size_t attached_count;
  parent_->control_lock().Acquire();

  if (port_id.base >= attached_ports_.size()) {
    parent_->control_lock().Release();
    return ZX_ERR_INVALID_ARGS;
  }
  std::optional<AttachedPort>& slot = attached_ports_[port_id.base];
  if (slot.has_value()) {
    parent_->control_lock().Release();
    return ZX_ERR_ALREADY_EXISTS;
  }

  zx::result<AttachedPort> acquire_port = parent_->AcquirePort(port_id, frame_types);
  if (acquire_port.is_error()) {
    parent_->control_lock().Release();
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
  // The newly attached port is the only port we're attached to, notify the parent that we want to
  // start up and kick the tx thread.
  if (attached_count == 1) {
    paused_.store(false);
    // NB: SessionStarted releases the control lock.
    parent_->SessionStarted(*this);
    parent_->tx_queue().Resume();
  } else {
    parent_->control_lock().Release();
  }

  return ZX_OK;
}

zx_status_t Session::DetachPort(netdev::wire::PortId port_id) {
  parent_->control_lock().Acquire();
  auto result = DetachPortLocked(port_id.base, port_id.salt);
  if (result.is_error()) {
    parent_->control_lock().Release();
    return result.error_value();
  }
  bool stop_session = result.value();

  // The newly detached port was the last one standing, notify parent we're a stopped session now.
  if (stop_session) {
    paused_.store(true);
    // NB: SessionStopped releases the control lock.
    parent_->SessionStopped(*this);
  } else {
    parent_->control_lock().Release();
  }
  return ZX_OK;
}

zx::result<bool> Session::DetachPortLocked(uint8_t port_id, std::optional<uint8_t> salt) {
  if (port_id >= attached_ports_.size()) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  std::optional<AttachedPort>& slot = attached_ports_[port_id];
  if (!slot.has_value()) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  AttachedPort& attached_port = slot.value();
  attached_port.AssertParentControlLockShared(*parent_);
  if (salt.has_value()) {
    if (!attached_port.SaltMatches(salt.value())) {
      return zx::error(ZX_ERR_NOT_FOUND);
    }
  }
  attached_port.WithPort([](DevicePort& p) { p.SessionDetached(); });
  slot = std::nullopt;
  return zx::ok(
      std::all_of(attached_ports_.begin(), attached_ports_.end(),
                  [](const std::optional<AttachedPort>& port) { return !port.has_value(); }));
}

bool Session::OnPortDestroyed(uint8_t port_id) {
  zx::result status = DetachPortLocked(port_id, std::nullopt);
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

void Session::Attach(AttachRequestView request, AttachCompleter::Sync& completer) {
  zx_status_t status =
      AttachPort(request->port, cpp20::span(request->rx_frames.data(), request->rx_frames.count()));
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void Session::Detach(DetachRequestView request, DetachCompleter::Sync& completer) {
  zx_status_t status = DetachPort(request->port);
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void Session::Close(CloseCompleter::Sync& _completer) { Kill(); }

void Session::MarkTxReturnResult(uint16_t descriptor_index, zx_status_t status) {
  buffer_descriptor_t& desc = descriptor(descriptor_index);
  using netdev::wire::TxReturnFlags;
  switch (status) {
    case ZX_OK:
      desc.return_flags = 0;
      break;
    case ZX_ERR_NOT_SUPPORTED:
      desc.return_flags =
          static_cast<uint32_t>(TxReturnFlags::kTxRetNotSupported | TxReturnFlags::kTxRetError);
      break;
    case ZX_ERR_NO_RESOURCES:
      desc.return_flags =
          static_cast<uint32_t>(TxReturnFlags::kTxRetOutOfResources | TxReturnFlags::kTxRetError);
      break;
    case ZX_ERR_UNAVAILABLE:
      desc.return_flags =
          static_cast<uint32_t>(TxReturnFlags::kTxRetNotAvailable | TxReturnFlags::kTxRetError);
      break;
    case ZX_ERR_INTERNAL:
      // ZX_ERR_INTERNAL should never assume any flag semantics besides generic error.
      __FALLTHROUGH;
    default:
      desc.return_flags = static_cast<uint32_t>(TxReturnFlags::kTxRetError);
      break;
  }
}

void Session::ReturnTxDescriptors(const uint16_t* descriptors, size_t count) {
  size_t actual_count;

  // TODO(https://fxbug.dev/32098): We're assuming that writing to the FIFO here
  // is a sufficient memory barrier for the other end to access the data. That
  // is currently true but not really guaranteed by the API.
  zx_status_t status = fifo_tx_.write(sizeof(uint16_t), descriptors, count, &actual_count);
  constexpr char kLogFormat[] = "%s: failed to return %ld tx descriptors: %s";
  switch (status) {
    case ZX_OK:
      if (actual_count != count) {
        LOGF_ERROR("%s: failed to return %ld/%ld tx descriptors", name(), count - actual_count,
                   count);
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
  LOGF_TRACE("%s: %s available:%ld transaction:%d", name(), __FUNCTION__, rx_avail_queue_count_,
             transact.remaining());
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
  buffer_descriptor_t* desc_ptr = checked_descriptor(descriptor_index);
  if (!desc_ptr) {
    LOGF_ERROR("%s: received out of bounds descriptor: %d", name(), descriptor_index);
    return ZX_ERR_INVALID_ARGS;
  }
  buffer_descriptor_t& desc = *desc_ptr;

  // chain_length is the number of buffers to follow. Rx buffers are always single buffers.
  if (desc.chain_length != 0) {
    LOGF_ERROR("%s: received invalid chain length for rx buffer: %d", name(), desc.chain_length);
    return ZX_ERR_INVALID_ARGS;
  }
  if (desc.data_length < parent_->info().min_rx_buffer_length) {
    LOGF_ERROR("netwok-device(%s): rx buffer length %d less than required minimum of %d", name(),
               desc.data_length, parent_->info().min_rx_buffer_length);
    return ZX_ERR_INVALID_ARGS;
  }
  *buff = {
      .id = buff->id,
      .region =
          {
              .vmo = vmo_id_,
              .offset = desc.offset + desc.head_length,
              .length = desc.data_length + desc.tail_length,
          },
  };
  return ZX_OK;
}

bool Session::CompleteRx(const RxFrameInfo& frame_info) {
  ZX_ASSERT(IsPrimary());

  // Always mark buffers as returned upon completion.
  auto defer = fit::defer([this, &frame_info]() { RxReturned(frame_info.buffers.size()); });

  // Copy session data to other sessions (if any) even if this session is paused.
  parent_->CopySessionData(*this, frame_info);

  // Allow the buffer to be reused as long as our rx path is still valid.
  bool allow_reuse = rx_valid_;

  if (IsSubscribedToFrameType(frame_info.meta.port,
                              static_cast<netdev::wire::FrameType>(frame_info.meta.frame_type)) &&
      !paused_.load()) {
    // Allow reuse if any issue happens loading descriptor configuration.
    // NB: Error logging happens at LoadRxInfo at a greater granularity, we only care about success
    // here.
    allow_reuse &= LoadRxInfo(frame_info) != ZX_OK;
  } else if (frame_info.meta.frame_type == 0) {
    // Help parent driver authors to debug common contract violation.
    LOGF_WARN("%s: rx frame has unspecified frame type, dropping frame", name());
  }

  return allow_reuse;
}

bool Session::CompleteRxWith(const Session& owner, const RxFrameInfo& frame_info) {
  // Shouldn't call CompleteRxWith where owner is self. Assertion enforces that
  // DeviceInterface::CopySessionData does the right thing.
  ZX_ASSERT(&owner != this);
  if (!IsSubscribedToFrameType(frame_info.meta.port,
                               static_cast<netdev::wire::FrameType>(frame_info.meta.frame_type)) ||
      IsPaused()) {
    if (frame_info.meta.frame_type == 0) {
      // Help parent driver authors to debug common contract violation.
      LOGF_WARN("%s: rx frame has unspecified frame type, dropping frame", name());
    }
    // Don't do anything if we're paused or not subscribed to this frame type.
    return false;
  }

  // Allocate enough descriptors to fit all the data that we want to copy from the other session.
  std::array<SessionRxBuffer, MAX_BUFFER_PARTS> parts_storage;
  auto parts_iter = parts_storage.begin();
  uint16_t* rx_queue_pick = &rx_avail_queue_[rx_avail_queue_count_];
  uint32_t remaining = frame_info.total_length;
  bool attempted_fetch = false;
  while (remaining != 0) {
    if (parts_iter == parts_storage.end()) {
      // Chained too many parts, this session is not providing buffers large enough.
      LOGF_WARN(
          "%s: failed to allocate %d bytes with %ld buffer parts (%d bytes "
          "remaining); frame dropped",
          name(), frame_info.total_length, parts_storage.size(), remaining);
      return false;
    }
    if (rx_avail_queue_count_ == 0) {
      // We allow a fetch attempt only once, which gives the session a chance to have returned
      // enough descriptors for this chained case.
      if (attempted_fetch) {
        return false;
      }
      attempted_fetch = true;

      // Can't do much if we can't fetch more descriptors. We have to drop this frame.
      // We intentionally don't log here because this becomes too noisy.
      // TODO(https://fxbug.dev/74434): Log here sparingly as part of "no buffers available"
      // strategy.
      if (FetchRxDescriptors() != ZX_OK) {
        return false;
      }

      // FetchRxDescriptors modifies the available rx queue, we need to build the parts again.
      remaining = frame_info.total_length;
      parts_iter = parts_storage.begin();
      rx_queue_pick = &rx_avail_queue_[rx_avail_queue_count_];
      continue;
    }
    SessionRxBuffer& session_buffer = *parts_iter++;
    session_buffer.descriptor = *(--rx_queue_pick);
    buffer_descriptor_t* desc_ptr = checked_descriptor(session_buffer.descriptor);
    if (!desc_ptr) {
      LOGF_TRACE("%s: descriptor %d out of range %d", name(), session_buffer.descriptor,
                 descriptor_count_);
      Kill();
      return false;
    }
    buffer_descriptor_t& desc = *desc_ptr;
    uint32_t desc_length = desc.data_length + desc.head_length + desc.tail_length;
    session_buffer.offset = 0;
    session_buffer.length = std::min(desc_length, remaining);
    remaining -= session_buffer.length;
  }

  cpp20::span<const SessionRxBuffer> parts(parts_storage.begin(), parts_iter);
  zx_status_t status = LoadRxInfo(RxFrameInfo{
      .meta = frame_info.meta,
      .port_id_salt = frame_info.port_id_salt,
      .buffers = parts,
      .total_length = frame_info.total_length,
  });
  // LoadRxInfo only fails if we can't fulfill the total length with the given buffer parts. It
  // shouldn't fail here because we hand crafted the parts above to fulfill the total frame length.
  ZX_ASSERT_MSG(status == ZX_OK, "failed to load frame information to copy session: %s",
                zx_status_get_string(status));
  rx_avail_queue_count_ -= parts.size();

  const uint16_t first_descriptor = parts.begin()->descriptor;

  // Copy the data from the owner VMO into our own.
  // We can assume that the owner descriptor is valid, because the descriptor was validated when
  // passing it down to the device.
  // Also, we know that our own descriptor is valid, because we already pre-loaded the
  // information by calling LoadRxInfo above.
  // The rx information from the owner session has not yet been loaded into its descriptor at this
  // point; iteration over buffer parts and offset/length information must be retrieved from
  // |frame_info|. The owner's descriptors provides only the original vmo offset to use, dictated by
  // the owner session's client.
  auto owner_rx_iter = frame_info.buffers.begin();
  auto get_vmo_owner_offset = [&owner](uint16_t index) -> uint64_t {
    const buffer_descriptor_t& desc = owner.descriptor(index);
    return desc.offset + desc.head_length;
  };
  uint64_t owner_vmo_offset = get_vmo_owner_offset(owner_rx_iter->descriptor);

  buffer_descriptor_t* desc_iter = &descriptor(first_descriptor);

  remaining = frame_info.total_length;
  uint32_t owner_off = 0;
  uint32_t self_off = 0;
  for (;;) {
    buffer_descriptor_t& desc = *desc_iter;
    const SessionRxBuffer& owner_rx_buffer = *owner_rx_iter;
    uint32_t owner_len = owner_rx_buffer.length - owner_off;
    uint32_t self_len = desc.data_length - self_off;
    uint32_t copy_len = owner_len >= self_len ? self_len : owner_len;
    cpp20::span target = data_at(desc.offset + desc.head_length + self_off, copy_len);
    cpp20::span src =
        owner.data_at(owner_vmo_offset + owner_rx_buffer.offset + owner_off, copy_len);
    std::copy_n(src.begin(), std::min(target.size(), src.size()), target.begin());

    owner_off += copy_len;
    self_off += copy_len;
    ZX_ASSERT(owner_off <= owner_rx_iter->length);
    ZX_ASSERT(self_off <= desc.data_length);

    remaining -= copy_len;
    if (remaining == 0) {
      return true;
    }

    if (self_off == desc.data_length) {
      desc_iter = &descriptor(desc.nxt);
      self_off = 0;
    }
    if (owner_off == owner_rx_buffer.length) {
      owner_vmo_offset = get_vmo_owner_offset((++owner_rx_iter)->descriptor);
      owner_off = 0;
    }
  }
}

bool Session::CompleteUnfulfilledRx() {
  RxReturned(1);
  return rx_valid_;
}

bool Session::ListenFromTx(const Session& owner, uint16_t owner_index) {
  ZX_ASSERT(&owner != this);
  if (IsPaused()) {
    // Do nothing if we're paused.
    return false;
  }

  // NB: This method is called before the tx frame is operated on for regular tx flow. We can't
  // assume that descriptors have already been validated.
  const buffer_descriptor_t* owner_desc_iter = owner.checked_descriptor(owner_index);
  if (!owner_desc_iter) {
    // Stop the listen short, validation will happen again on regular tx flow.
    return false;
  }
  const buffer_descriptor_t& owner_desc = *owner_desc_iter;
  // Bail early if not interested in frame type.
  if (!IsSubscribedToFrameType(owner_desc.port_id.base,
                               static_cast<netdev::wire::FrameType>(owner_desc.frame_type))) {
    return false;
  }

  BufferParts<SessionRxBuffer> parts;
  auto parts_iter = parts.begin();
  uint32_t total_length = 0;
  for (;;) {
    const buffer_descriptor_t& owner_part = *owner_desc_iter;
    *parts_iter++ = {
        .descriptor = owner_index,
        .length = owner_part.data_length,
    };
    total_length += owner_part.data_length;
    if (owner_part.chain_length == 0) {
      break;
    }
    owner_desc_iter = owner.checked_descriptor(owner_part.nxt);
    if (!owner_desc_iter) {
      // Let regular tx validation punish the owner session.
      return false;
    }
  }

  auto info_type = static_cast<netdev::wire::InfoType>(owner_desc.info_type);
  switch (info_type) {
    case netdev::wire::InfoType::kNoInfo:
      break;
    default:
      LOGF_ERROR("%s: info type (%d) not recognized, discarding information", name(),
                 owner_desc.info_type);
      info_type = netdev::wire::InfoType::kNoInfo;
      break;
  }
  // Build frame information as if this had been received from any other session and call into
  // common routine to commit the descriptor.
  const buffer_metadata_t frame_meta = {
      .port = owner_desc.port_id.base,
      .info_type = static_cast<uint32_t>(info_type),
      .flags = static_cast<uint32_t>(netdev::wire::RxFlags::kRxEchoedTx),
      .frame_type = owner_desc.frame_type,
  };

  return CompleteRxWith(owner, RxFrameInfo{
                                   .meta = frame_meta,
                                   .port_id_salt = parent_->GetPortSalt(frame_meta.port),
                                   .buffers = cpp20::span(parts.begin(), parts_iter),
                                   .total_length = total_length,
                               });
}

zx_status_t Session::LoadRxInfo(const RxFrameInfo& info) {
  // Expected to have been checked at upper layers. See RxQueue::CompleteRxList.
  // - Buffer parts does not violate maximum parts contract.
  // - No empty frames must reach us here.
  ZX_DEBUG_ASSERT(info.buffers.size() <= netdev::wire::kMaxDescriptorChain);
  ZX_DEBUG_ASSERT(!info.buffers.empty());

  auto buffers_iterator = info.buffers.end();
  uint8_t chain_len = 0;
  uint16_t next_desc_index = 0xFFFF;
  for (;;) {
    buffers_iterator--;
    const SessionRxBuffer& buffer = *buffers_iterator;
    buffer_descriptor_t& desc = descriptor(buffer.descriptor);
    uint32_t available_len = desc.data_length + desc.head_length + desc.tail_length;
    // Total consumed length for the descriptor is the offset + length because length is counted
    // from the offset on fulfilled buffer parts.
    uint32_t consumed_part_length = buffer.offset + buffer.length;
    if (consumed_part_length > available_len) {
      LOGF_ERROR("%s: invalid returned buffer length: %d, descriptor fits %d", name(),
                 consumed_part_length, available_len);
      return ZX_ERR_INVALID_ARGS;
    }
    // NB: Update only the fields that we need to update here instead of using literals; we're
    // writing into shared memory and we don't want to write over all fields nor trust compiler
    // optimizations to elide "a = a" statements.
    desc.head_length = static_cast<uint16_t>(buffer.offset);
    desc.data_length = buffer.length;
    desc.tail_length = static_cast<uint16_t>(available_len - consumed_part_length);
    desc.chain_length = chain_len;
    desc.nxt = next_desc_index;
    chain_len++;
    next_desc_index = buffer.descriptor;

    if (buffers_iterator == info.buffers.begin()) {
      // The descriptor pointer now points to the first descriptor in the chain, where we store the
      // metadata.
      auto info_type = static_cast<netdev::wire::InfoType>(info.meta.info_type);
      switch (info_type) {
        case netdev::wire::InfoType::kNoInfo:
          break;
        default:
          LOGF_ERROR("%s: info type (%d) not recognized, discarding information", name(),
                     info.meta.info_type);
          info_type = netdev::wire::InfoType::kNoInfo;
          break;
      }
      desc.info_type = static_cast<uint32_t>(info_type);
      desc.frame_type = info.meta.frame_type;
      desc.inbound_flags = info.meta.flags;
      desc.port_id = {
          .base = info.meta.port,
          .salt = info.port_id_salt,
      };

      rx_return_queue_[rx_return_queue_count_++] = buffers_iterator->descriptor;
      return ZX_OK;
    }
  }
}

void Session::CommitRx() {
  if (rx_return_queue_count_ == 0 || paused_.load()) {
    return;
  }
  size_t actual;

  // TODO(https://fxbug.dev/32098): We're assuming that writing to the FIFO here
  // is a sufficient memory barrier for the other end to access the data. That
  // is currently true but not really guaranteed by the API.
  zx_status_t status = fifo_rx_->fifo.write(sizeof(uint16_t), rx_return_queue_.get(),
                                            rx_return_queue_count_, &actual);
  constexpr char kLogFormat[] = "%s: failed to return %ld rx descriptors: %s";
  switch (status) {
    case ZX_OK:
      if (actual != rx_return_queue_count_) {
        LOGF_ERROR("%s: failed to return %ld/%ld rx descriptors", name(),
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

bool Session::IsSubscribedToFrameType(uint8_t port, netdev::wire::FrameType frame_type) {
  if (port >= attached_ports_.size()) {
    return false;
  }
  std::optional<AttachedPort>& slot = attached_ports_[port];
  if (!slot.has_value()) {
    return false;
  }
  cpp20::span subscribed = slot.value().frame_types();
  return std::any_of(subscribed.begin(), subscribed.end(),
                     [frame_type](const netdev::wire::FrameType& t) { return t == frame_type; });
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
