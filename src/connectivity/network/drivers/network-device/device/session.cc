// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "session.h"

#include <lib/fidl/epitaph.h>
#include <zircon/device/network.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/ref_counted.h>

#include "device_interface.h"
#include "log.h"

namespace network::internal {

constexpr uint32_t kKillTxKey = 0;
constexpr uint32_t kResumeTxKey = 1;
constexpr uint32_t kTxAvailKey = 2;

bool Session::IsListen() const {
  return static_cast<bool>(flags_ & netdev::SessionFlags::LISTEN_TX);
}

bool Session::IsPrimary() const {
  return static_cast<bool>(flags_ & netdev::SessionFlags::PRIMARY);
}

bool Session::IsPaused() const { return paused_; }

bool Session::ShouldTakeOverPrimary(const Session* current_primary) const {
  if ((!IsPrimary()) || current_primary == this) {
    // if we're not a primary session, or the primary is already ourselves, then we don't
    // want to take over.
    return false;
  } else if (!current_primary) {
    // always request to take over if there is no current primary session.
    return true;
  } else if (current_primary->IsPaused() && !IsPaused()) {
    // If the current primary session is paused, but we aren't we can take it over.
    return true;
  } else {
    // otherwise, the heuristic to apply here is that we want to use the
    // session that has the largest number of descriptors defined, as that relates to having more
    // buffers available for us.
    return descriptor_count_ > current_primary->descriptor_count_;
  }
}

zx_status_t Session::Create(async_dispatcher_t* dispatcher, netdev::SessionInfo info,
                            fidl::StringView name, DeviceInterface* parent, zx::channel control,
                            std::unique_ptr<Session>* out_session, netdev::Fifos* out_fifos) {
  fbl::AllocChecker checker;

  for (const auto& rx_request : info.rx_frames) {
    if (!parent->IsValidRxFrameType(static_cast<uint8_t>(rx_request))) {
      return ZX_ERR_INVALID_ARGS;
    }
  }

  if (info.descriptor_version != NETWORK_DEVICE_DESCRIPTOR_VERSION) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  std::unique_ptr<Session> session(new (&checker)
                                       Session(dispatcher, &info, std::move(name), parent));
  if (!checker.check()) {
    LOGF_ERROR("network-device: Failed to allocate session");
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status;
  if ((status = session->Init(out_fifos)) != ZX_OK) {
    LOGF_ERROR("network-device: Failed to init session %s: %s", session->name(),
               zx_status_get_string(status));
    out_session->reset(nullptr);
    return status;
  }

  if ((status = session->Bind(std::move(control))) != ZX_OK) {
    LOGF_ERROR("network-device: Failed to bind session %s: %s", session->name(),
               zx_status_get_string(status));
    return status;
  }

  if ((status = parent->RegisterDataVmo(std::move(info.data), &session->vmo_id_,
                                        &session->data_vmo_)) != ZX_OK) {
    return status;
  }

  *out_session = std::move(session);
  return ZX_OK;
}

Session::Session(async_dispatcher_t* dispatcher, netdev::SessionInfo* info, fidl::StringView name,
                 DeviceInterface* parent)
    : dispatcher_(dispatcher),
      vmo_descriptors_(std::move(info->descriptors)),
      paused_(true),
      descriptor_count_(info->descriptor_count),
      descriptor_length_(info->descriptor_length * sizeof(uint64_t)),
      flags_(info->options),
      frame_type_count_(static_cast<uint32_t>(info->rx_frames.count())),
      parent_(parent) {
  ZX_ASSERT(frame_type_count_ <= netdev::MAX_FRAME_TYPES);
  for (uint32_t i = 0; i < frame_type_count_; i++) {
    frame_types_[i] = static_cast<uint8_t>(info->rx_frames[i]);
  }

  auto* end = std::copy(name.begin(), name.end(), name_.begin());
  *end = '\0';
}

Session::~Session() {
  // Stop the Tx thread if it hasn't been stopped already. We need to do this on destruction in case
  // binding the control channel to the dispatcher fails.
  StopTxThread();
  ZX_ASSERT(in_flight_rx_ == 0);
  ZX_ASSERT(in_flight_tx_ == 0);
  ZX_ASSERT(vmo_id_ == MAX_VMOS);
  // attempts to send an epitaph, signaling that the buffers are reclaimed:
  if (control_channel_.has_value()) {
    fidl_epitaph_write(control_channel_->get(), ZX_ERR_CANCELED);
  }

  LOGF_TRACE("network-device(%s): Session destroyed", name());
}

zx_status_t Session::Init(netdev::Fifos* out) {
  // Map the data and descriptors VMO:

  zx_status_t status;
  if ((status = descriptors_.Map(vmo_descriptors_, 0, descriptor_count_ * descriptor_length_,
                                 ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_REQUIRE_NON_RESIZABLE,
                                 nullptr)) != ZX_OK) {
    LOGF_ERROR("network-device(%s): failed to map data VMO: %s", name(),
               zx_status_get_string(status));
    return status;
  }

  // create the FIFOs
  fbl::AllocChecker ac;
  auto* rx_fifo = new (&ac) RefCountedFifo;
  if (!ac.check()) {
    LOGF_ERROR("network-device(%s): failed to allocate ", name());
    return ZX_ERR_NO_MEMORY;
  }

  if (zx::fifo::create(parent_->rx_fifo_depth(), sizeof(uint16_t), 0, &out->rx, &rx_fifo->fifo) !=
          ZX_OK ||
      zx::fifo::create(parent_->tx_fifo_depth(), sizeof(uint16_t), 0, &out->tx, &fifo_tx_) !=
          ZX_OK) {
    LOGF_ERROR("network-device(%s): failed to create FIFOS", name());
    return ZX_ERR_NO_RESOURCES;
  } else if (zx::port::create(0, &tx_port_) != ZX_OK) {
    LOGF_ERROR("network-device(%s): failed to create tx port", name());
    return ZX_ERR_NO_RESOURCES;
  }

  fifo_rx_ = fbl::AdoptRef(rx_fifo);

  rx_return_queue_.reset(new (&ac) uint16_t[parent_->rx_fifo_depth()]);
  if (!ac.check()) {
    LOGF_ERROR("network-device(%s): failed to create return queue", name());
    return ZX_ERR_NO_MEMORY;
  }
  rx_return_queue_count_ = 0;

  rx_avail_queue_.reset(new (&ac) uint16_t[parent_->rx_fifo_depth()]);
  if (!ac.check()) {
    LOGF_ERROR("network-device(%s): failed to create return queue", name());
    return ZX_ERR_NO_MEMORY;
  }
  rx_avail_queue_count_ = 0;

  thrd_t thread;
  if (thrd_create_with_name(
          &thread, [](void* ctx) { return reinterpret_cast<Session*>(ctx)->Thread(); },
          reinterpret_cast<void*>(this), "netdevice:session") != thrd_success) {
    LOGF_ERROR("network-device(%s): session failed to create thread", name());
    return ZX_ERR_INTERNAL;
  }
  thread_ = thread;

  LOGF_TRACE(
      "network-device(%s): starting session:"
      " descriptor_count: %d,"
      " descriptor_length: %ld,"
      " flags: %08X",
      name(), descriptor_count_, descriptor_length_, static_cast<uint16_t>(flags_));
  return ZX_OK;
}

zx_status_t Session::Bind(zx::channel channel) {
  auto result = fidl::BindServer(
      dispatcher_, std::move(channel), this,
      fidl::OnUnboundFn<Session>([](Session* self, fidl::UnbindInfo info, zx::channel channel) {
        self->OnUnbind(info.reason, std::move(channel));
      }));
  if (result.is_ok()) {
    binding_ = result.take_value();
    return ZX_OK;
  } else {
    return result.error();
  }
}

void Session::OnUnbind(fidl::UnbindInfo::Reason reason, zx::channel channel) {
  LOGF_TRACE("network-device(%s): session unbound, reason=%d", name(), reason);

  // Stop the Tx thread immediately, so we stop fetching more tx buffers from the client.
  StopTxThread();

  // Close the Tx FIFO so no more data operations can occur. The session may linger around for a
  // short while still if the device implementation is holding on to buffers on the session's VMO.
  // When the session is destroyed, it'll attempt to send an epitaph message over the channel if
  // it's still open. The Rx FIFO is not closed here since it's possible it's currently shared with
  // the Rx Queue. The session will drop its reference to the Rx FIFO upon destruction.
  fifo_tx_.reset();

  switch (reason) {
    case fidl::UnbindInfo::kUnbind:
    case fidl::UnbindInfo::kDispatcherError:
    case fidl::UnbindInfo::kChannelError:
    case fidl::UnbindInfo::kEncodeError:
    case fidl::UnbindInfo::kDecodeError:
    case fidl::UnbindInfo::kUnexpectedMessage:
      // Store the channel to send an epitaph once the session is destroyed.
      control_channel_ = std::move(channel);
      break;
    case fidl::UnbindInfo::kClose:
    case fidl::UnbindInfo::kPeerClosed:
      break;
  }

  // NOTE: the parent may destroy the session synchronously in NotifyDeadSession, this is the
  // last thing we can do safely with this session object.
  parent_->NotifyDeadSession(this);
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
  zx_status_t status =
      fifo_tx_.read(sizeof(uint16_t), fetch_buffer, transaction.available(), &read);
  if (status != ZX_OK) {
    if (status != ZX_ERR_SHOULD_WAIT) {
      LOGF_TRACE("network-device(%s): tx fifo read failed %s", name(),
                 zx_status_get_string(status));
    }
    return status;
  }

  uint16_t* desc_idx = fetch_buffer;
  auto req_header_length = parent_->info().tx_head_length;
  auto req_tail_length = parent_->info().tx_tail_length;

  bool notify_listeners = false;

  while (read > 0) {
    auto* desc = descriptor(*desc_idx);
    if (!desc) {
      LOGF_ERROR("network-device(%s): received out of bounds descriptor: %d", name(), *desc_idx);
      return ZX_ERR_IO_INVALID;
    }

    // Reject invalid tx types
    if (!parent_->IsValidTxFrameType(desc->frame_type)) {
      LOGF_ERROR("network-device(%s): received invalid tx frame type: %d", name(),
                 desc->frame_type);
      return ZX_ERR_IO_INVALID;
    }

    auto* buffer = transaction.GetBuffer();
    buffer->meta.flags = desc->inbound_flags;
    buffer->meta.frame_type = desc->frame_type;
    buffer->meta.info_type = desc->info_type;
    if (buffer->meta.info_type != static_cast<uint32_t>(netdev::InfoType::NO_INFO)) {
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
    if (desc->chain_length >= netdev::MAX_DESCRIPTOR_CHAIN) {
      LOGF_ERROR("network-device(%s): received invalid chain length: %d", name(),
                 desc->chain_length);
      return ZX_ERR_IO_INVALID;
    }
    auto expect_chain = desc->chain_length;

    bool add_head_space = buffer->head_length != 0;
    uint16_t didx = *desc_idx;
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
      buffer->data.parts_count++;

      add_head_space = false;
      if (expect_chain == 0) {
        break;
      } else {
        didx = desc->nxt;
        desc = descriptor(didx);
        if (desc == nullptr) {
          LOGF_ERROR("network-device(%s): invalid chained descriptor index: %d", name(), didx);
          return ZX_ERR_IO_INVALID;
        } else if (desc->chain_length != expect_chain - 1) {
          LOGF_ERROR("network-device(%s): invalid next chain length %d on descriptor %d", name(),
                     desc->chain_length, didx);
          return ZX_ERR_IO_INVALID;
        }
        expect_chain--;
      }
    }
    // notifty parent so we can copy this to any listening sessions
    notify_listeners |= parent_->ListenSessionData(*this, *desc_idx);
    transaction.Push(*desc_idx);
    desc_idx++;
    read--;
  }

  if (notify_listeners) {
    parent_->CommitAllSessions();
  }

  return transaction.overrun() ? ZX_ERR_IO_OVERRUN : ZX_OK;
}

buffer_descriptor_t* Session::descriptor(uint16_t index) {
  if (index < descriptor_count_) {
    return reinterpret_cast<buffer_descriptor_t*>(static_cast<uint8_t*>(descriptors_.start()) +
                                                  (index * descriptor_length_));
  } else {
    return nullptr;
  }
}

const buffer_descriptor_t* Session::descriptor(uint16_t index) const {
  if (index < descriptor_count_) {
    return reinterpret_cast<buffer_descriptor_t*>(static_cast<uint8_t*>(descriptors_.start()) +
                                                  (index * descriptor_length_));
  } else {
    return nullptr;
  }
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

void Session::SetPaused(bool paused, SetPausedCompleter::Sync& _completer) {
  bool old = paused_.exchange(paused);
  if (paused != old) {
    // NOTE: SetPaused is served from the same thread as we operate the Tx FIFO on, so we can
    // ensure that the Tx FIFO will not be running until we return from here. If that changes, we
    // need to split the paused boolean into two signals to prevent a race.
    if (paused) {
      parent_->SessionStopped(this);
    } else {
      parent_->SessionStarted(this);
    }

    if (!paused) {
      // If we're unpausing a session, signal that we want to resume Tx:
      ResumeTx();
    }
  }
}

void Session::Close(CloseCompleter::Sync& _completer) { Kill(); }

void Session::MarkTxReturnResult(uint16_t descriptor_index, zx_status_t status) {
  ZX_ASSERT(descriptor_index < descriptor_count_);
  auto* desc = descriptor(descriptor_index);
  switch (status) {
    case ZX_OK:
      desc->return_flags = 0;
      break;
    case ZX_ERR_NOT_SUPPORTED:
      desc->return_flags = static_cast<uint32_t>(netdev::TxReturnFlags::TX_RET_NOT_SUPPORTED |
                                                 netdev::TxReturnFlags::TX_RET_ERROR);
      break;
    case ZX_ERR_NO_RESOURCES:
      desc->return_flags = static_cast<uint32_t>(netdev::TxReturnFlags::TX_RET_OUT_OF_RESOURCES |
                                                 netdev::TxReturnFlags::TX_RET_ERROR);
      break;
    case ZX_ERR_UNAVAILABLE:
      desc->return_flags = static_cast<uint32_t>(netdev::TxReturnFlags::TX_RET_NOT_AVAILABLE |
                                                 netdev::TxReturnFlags::TX_RET_ERROR);
      break;
    default:
      desc->return_flags = static_cast<uint32_t>(netdev::TxReturnFlags::TX_RET_ERROR);
      break;
  }
}

void Session::ReturnTxDescriptors(const uint16_t* descriptors, uint32_t count) {
  size_t actual_count = 0;
  zx_status_t status;
  if ((status = fifo_tx_.write(sizeof(uint16_t), descriptors, count, &actual_count)) != ZX_OK ||
      actual_count != count) {
    LOGF_WARN("network-device(%s): failed to return %ld tx descriptors: %s", name(),
              count - actual_count, zx_status_get_string(status));
    // given that we always tightly control the amount of stuff written to the FIFOs. We don't
    // expect this to happen during regular operation, unless there's a bug somewhere. This line is
    // expected to fire, though, if the tx FIFO is closed while we're trying to return buffers.
  }
}

bool Session::LoadAvailableRxDescriptors(RxQueue::SessionTransaction* transact) {
  if (rx_avail_queue_count_ == 0) {
    return false;
  }
  while (transact->remaining() != 0 && rx_avail_queue_count_ != 0) {
    rx_avail_queue_count_--;
    transact->Push(this, rx_avail_queue_[rx_avail_queue_count_]);
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

zx_status_t Session::LoadRxDescriptors(RxQueue::SessionTransaction* transact) {
  if (rx_avail_queue_count_ == 0) {
    zx_status_t status = FetchRxDescriptors();
    if (status != ZX_OK) {
      return status;
    }
  }
  // If FetchRxDescriptors succeeded, that means we MUST be able to load available.
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
  if (desc->chain_length >= netdev::MAX_DESCRIPTOR_CHAIN) {
    LOGF_ERROR("network-device(%s): received invalid chain length: %d", name(), desc->chain_length);
    return ZX_ERR_INVALID_ARGS;
  }
  buff->data.parts_count = 0;

  // We need the const cast here to go around banjo code generation. It looks very ugly, though.
  auto* buffer_parts = const_cast<buffer_region_t*>(buff->data.parts_list);
  buff->data.vmo_id = vmo_id_;

  auto expect_chain = desc->chain_length;
  uint16_t didx = descriptor_index;
  for (;;) {
    buffer_parts->offset = desc->offset + desc->head_length;
    buffer_parts->length = desc->data_length;
    buff->data.parts_count++;
    buffer_parts++;
    if (expect_chain == 0) {
      break;
    } else {
      didx = desc->nxt;
      desc = descriptor(didx);
      if (desc == nullptr) {
        LOGF_ERROR("network-device(%s): invalid chained descriptor index: %d", name(), didx);
        return ZX_ERR_INVALID_ARGS;
      } else if (desc->chain_length != expect_chain - 1) {
        LOGF_ERROR("network-device(%s): invalid next chain length %d on descriptor %d", name(),
                   desc->chain_length, didx);
        return ZX_ERR_INVALID_ARGS;
      }
      expect_chain--;
    }
  }

  return ZX_OK;
}

bool Session::CompleteRx(uint16_t descriptor_index, const rx_buffer_t* buff) {
  ZX_ASSERT(IsPrimary());
  // if there's no data in the buffer, just immediately return and allow it to be reused.
  if (buff->total_length == 0) {
    return true;
  }

  bool ignore = !IsSubscribedToFrameType(buff->meta.frame_type) || paused_.load();

  // Copy session data to other sessions (if any) even if this session is paused.
  parent_->CopySessionData(*this, descriptor_index, buff);

  if (!ignore) {
    // we validated the descriptor coming in, writing it back should always work.
    ZX_ASSERT(LoadRxInfo(descriptor_index, buff) == ZX_OK);
    rx_return_queue_[rx_return_queue_count_++] = descriptor_index;
  }

  // allow the buffer to be immediately reused if we ignored the frame AND if our rx is still valid
  return ignore && rx_valid_;
}

void Session::CompleteRxWith(const Session& owner, uint16_t owner_index, const rx_buffer_t* buff) {
  // can't call this if owner is self.
  ZX_ASSERT(&owner != this);
  if (!IsSubscribedToFrameType(buff->meta.frame_type) || paused_.load()) {
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
  auto len = buff->total_length;
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
    // do nothing if we're paused
    return false;
  }

  if (rx_avail_queue_count_ == 0) {
    // can't do much if we can't fetch more descriptors
    if (FetchRxDescriptors() != ZX_OK) {
      LOGF_TRACE("network-device(%s): Failed to fetch rx descriptors for Tx listening", name());
      return false;
    }
  }
  // shouldn't get here without available descriptors
  ZX_ASSERT(rx_avail_queue_count_ > 0);
  rx_avail_queue_count_--;
  auto target_desc = rx_avail_queue_[rx_avail_queue_count_];

  auto* owner_desc = owner.descriptor(owner_index);
  auto* desc = descriptor(target_desc);
  // NOTE(brunodalbo) Do we want to listen on info as well?
  desc->info_type = static_cast<uint32_t>(netdev::InfoType::NO_INFO);
  desc->frame_type = owner_desc->frame_type;
  desc->return_flags = static_cast<uint32_t>(netdev::RxFlags::RX_ECHOED_TX);

  uint64_t my_offset = 0;
  uint64_t owner_offset = 0;
  // start copying the data over:
  while (owner_desc) {
    if (!desc) {
      // not enough space to put data
      break;
    }
    auto me_avail = desc->data_length - my_offset;
    auto owner_avail = owner_desc->data_length - owner_offset;
    auto copy = me_avail < owner_avail ? me_avail : owner_avail;

    auto target = data_at(desc->offset + desc->head_length + my_offset, copy);
    auto src = owner.data_at(owner_desc->offset + owner_desc->head_length + owner_offset, copy);
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
    // did not reach end of data, just return descriptor to queue.
    rx_avail_queue_[rx_avail_queue_count_] = target_desc;
    rx_avail_queue_count_++;
    return false;
  }
  if (desc) {
    // set length in last buffer
    desc->data_length = static_cast<uint32_t>(my_offset);
  }

  // add the descriptor to the return queue.
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
  auto len = static_cast<uint32_t>(buff->total_length);

  if (buff->meta.info_type != static_cast<uint32_t>(netdev::InfoType::NO_INFO)) {
    LOGF_WARN("network-device(%s): InfoType not recognized :%d", name(), buff->meta.info_type);
  } else {
    desc->info_type = static_cast<uint32_t>(netdev::InfoType::NO_INFO);
  }
  desc->frame_type = buff->meta.frame_type;
  desc->inbound_flags = buff->meta.flags;
  if (desc->chain_length >= netdev::MAX_DESCRIPTOR_CHAIN) {
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
  zx_status_t status;
  size_t actual = 0;
  if ((status = fifo_rx_->fifo.write(sizeof(uint16_t), rx_return_queue_.get(),
                                     rx_return_queue_count_, &actual)) != ZX_OK ||
      actual != rx_return_queue_count_) {
    LOGF_WARN("network-device(%s): failed to return %ld tx descriptors: %s", name(),
              rx_return_queue_count_ - actual, zx_status_get_string(status));
    // given that we always tightly control the amount of stuff written to the FIFOs. We don't
    // expect this to happen during regular operation, unless there's a bug somewhere. This line is
    // expected to fire, though, if the rx FIFO is closed while we're trying to return buffers.
  }
  rx_return_queue_count_ = 0;
}

bool Session::IsSubscribedToFrameType(uint8_t frame_type) {
  auto* end = frame_types_.begin() + frame_type_count_;
  for (auto* i = frame_types_.begin(); i != end; i++) {
    if (*i == frame_type) {
      return true;
    }
  }
  return false;
}

uint8_t Session::ReleaseDataVmo() {
  uint8_t id = vmo_id_;
  // Reset identifier to the marker value. The destructor will assert that `ReleaseDataVmo` was
  // called by checking the value.
  vmo_id_ = MAX_VMOS;
  data_vmo_ = nullptr;
  return id;
}

}  // namespace network::internal
