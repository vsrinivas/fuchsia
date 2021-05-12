// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rx_queue.h"

#include <zircon/assert.h>

#include "log.h"
#include "session.h"

namespace network::internal {

RxQueue::~RxQueue() {
  // running_ is tied to the lifetime of the watch thread, it's cleared in`RxQueue::JoinThread`.
  // This assertion protects us from destruction paths where `RxQueue::JoinThread` is not called.
  ZX_ASSERT_MSG(!running_, "RxQueue destroyed without disposing of port and thread first.");
}

zx::status<std::unique_ptr<RxQueue>> RxQueue::Create(DeviceInterface* parent) {
  fbl::AllocChecker ac;
  std::unique_ptr<RxQueue> queue(new (&ac) RxQueue(parent));
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  fbl::AutoLock lock(&queue->parent_->rx_lock());
  // The RxQueue's capacity is the device's FIFO rx depth as opposed to the hardware's queue depth
  // so we can (possibly) reduce the amount of reads on the rx fifo during rx interrupts.
  auto capacity = parent->rx_fifo_depth();

  zx::status available_queue = RingQueue<uint32_t>::Create(capacity);
  if (available_queue.is_error()) {
    return available_queue.take_error();
  }
  queue->available_queue_ = std::move(available_queue.value());

  zx::status in_flight = IndexedSlab<InFlightBuffer>::Create(capacity);
  if (in_flight.is_error()) {
    return in_flight.take_error();
  }
  queue->in_flight_ = std::move(in_flight.value());

  auto device_depth = parent->info().rx_depth;

  queue->space_buffers_.reset(new (&ac) rx_space_buffer_t[device_depth]);
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  queue->buffer_parts_.reset(new (&ac) BufferParts[device_depth]);
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  for (uint32_t i = 0; i < device_depth; i++) {
    queue->space_buffers_[i].data.parts_list = queue->buffer_parts_[i].data();
  }

  zx_status_t status;
  if ((status = zx::port::create(0, &queue->rx_watch_port_)) != ZX_OK) {
    LOGF_ERROR("network-device: failed to create rx watch port: %s", zx_status_get_string(status));
    return zx::error(status);
  }

  thrd_t watch_thread;
  if (thrd_create_with_name(
          &watch_thread, [](void* ctx) { return reinterpret_cast<RxQueue*>(ctx)->WatchThread(); },
          reinterpret_cast<void*>(queue.get()), "netdevice:rx_watch") != thrd_success) {
    LOG_ERROR("network-device: rx queue failed to create thread");
    return zx::error(ZX_ERR_INTERNAL);
  }
  queue->rx_watch_thread_ = watch_thread;
  queue->running_ = true;
  return zx::ok(std::move(queue));
}

void RxQueue::TriggerRxWatch() {
  if (!running_) {
    return;
  }

  zx_port_packet_t packet;
  packet.type = ZX_PKT_TYPE_USER;
  packet.key = kTriggerRxKey;
  packet.status = ZX_OK;
  zx_status_t status = rx_watch_port_.queue(&packet);
  if (status != ZX_OK) {
    LOGF_ERROR("network-device: TriggerRxWatch failed: %s", zx_status_get_string(status));
  }
}

void RxQueue::TriggerSessionChanged() {
  if (!running_) {
    return;
  }
  zx_port_packet_t packet;
  packet.type = ZX_PKT_TYPE_USER;
  packet.key = kSessionSwitchKey;
  packet.status = ZX_OK;
  zx_status_t status = rx_watch_port_.queue(&packet);
  if (status != ZX_OK) {
    LOGF_ERROR("network-device: TriggerSessionChanged failed: %s", zx_status_get_string(status));
  }
}

void RxQueue::JoinThread() {
  if (rx_watch_thread_.has_value()) {
    zx_port_packet_t packet;
    packet.type = ZX_PKT_TYPE_USER;
    packet.key = kQuitWatchKey;
    zx_status_t status = rx_watch_port_.queue(&packet);
    if (status != ZX_OK) {
      LOGF_ERROR("network-device: RxQueue::JoinThread failed to send quit key: %s",
                 zx_status_get_string(status));
    }
    // Mark the queue as not running anymore.
    running_ = false;
    thrd_join(*std::exchange(rx_watch_thread_, std::nullopt), nullptr);
  }
}

void RxQueue::PurgeSession(Session& session) {
  fbl::AutoLock lock(&parent_->rx_lock());
  // Get rid of all available buffers that belong to the session and stop its rx path.
  session.AssertParentRxLock(*parent_);
  session.StopRx();
  for (auto nu = available_queue_->count(); nu > 0; nu--) {
    auto b = available_queue_->Pop();
    if (in_flight_->Get(b).session == &session) {
      in_flight_->Free(b);
    } else {
      // Push back to the end of the queue.
      available_queue_->Push(b);
    }
  }
}

std::tuple<RxQueue::InFlightBuffer*, uint32_t> RxQueue::GetBuffer() {
  if (available_queue_->count() != 0) {
    auto idx = available_queue_->Pop();
    return std::make_tuple(&in_flight_->Get(idx), idx);
  }
  // Need to fetch more from the session.
  if (in_flight_->available() == 0) {
    // No more space to keep in flight buffers.
    LOG_ERROR("network-device: can't fit more in-flight buffers");
    return std::make_tuple(nullptr, 0);
  }

  RxSessionTransaction transaction(this);
  switch (zx_status_t status = parent_->LoadRxDescriptors(transaction); status) {
    case ZX_OK:
      break;
    default:
      LOGF_ERROR("network-device: failed to load rx buffer descriptors: %s",
                 zx_status_get_string(status));
      __FALLTHROUGH;
    case ZX_ERR_PEER_CLOSED:  // Primary FIFO closed.
    case ZX_ERR_SHOULD_WAIT:  // No Rx buffers available in FIFO.
    case ZX_ERR_BAD_STATE:    // Primary session stopped or paused.
      return std::make_tuple(nullptr, 0);
  }
  // LoadRxDescriptors can't return OK if it couldn't load any descriptors.
  auto idx = available_queue_->Pop();
  return std::make_tuple(&in_flight_->Get(idx), idx);
}

zx_status_t RxQueue::PrepareBuff(rx_space_buffer_t* buff) {
  auto [session_buffer, index] = GetBuffer();
  if (session_buffer == nullptr) {
    return ZX_ERR_NO_RESOURCES;
  }
  zx_status_t status;
  buff->id = index;
  if ((status = session_buffer->session->FillRxSpace(session_buffer->descriptor_index, buff)) !=
      ZX_OK) {
    // If the session can't fill Rx for any reason, kill it.
    session_buffer->session->Kill();
    // Put the index back at the end of the available queue.
    available_queue_->Push(index);
    return status;
  }

  session_buffer->session->RxTaken();
  device_buffer_count_++;
  return ZX_OK;
}

void RxQueue::CompleteRxList(const rx_buffer_t* rx, size_t count) {
  fbl::AutoLock lock(&parent_->rx_lock());
  SharedAutoLock control_lock(&parent_->control_lock());
  device_buffer_count_ -= count;
  while (count--) {
    auto& session_buffer = in_flight_->Get(rx->id);
    session_buffer.session->AssertParentControlLockShared(*parent_);
    session_buffer.session->AssertParentRxLock(*parent_);
    if (session_buffer.session->CompleteRx(session_buffer.descriptor_index, rx)) {
      // We can reuse the descriptor.
      available_queue_->Push(rx->id);
    } else {
      in_flight_->Free(rx->id);
    }
    rx++;
  }
  parent_->CommitAllSessions();
  if (device_buffer_count_ <= parent_->rx_notify_threshold()) {
    TriggerRxWatch();
  }
}

int RxQueue::WatchThread() {
  auto loop = [this]() -> zx_status_t {
    fbl::RefPtr<RefCountedFifo> observed_fifo(nullptr);
    bool waiting_on_fifo = false;
    for (;;) {
      zx_port_packet_t packet;
      zx_status_t status;
      bool fifo_readable = false;
      if ((status = rx_watch_port_.wait(zx::time::infinite(), &packet)) != ZX_OK) {
        LOGF_ERROR("RxQueue::WatchThread port wait failed %s", zx_status_get_string(status));
        return status;
      }
      if (parent_->evt_rx_queue_packet) {
        parent_->evt_rx_queue_packet(packet.key);
      }
      switch (packet.key) {
        case kQuitWatchKey:
          LOG_TRACE("RxQueue::WatchThread got quit key");
          return ZX_OK;
        case kSessionSwitchKey:
          if (observed_fifo && waiting_on_fifo) {
            status = rx_watch_port_.cancel(observed_fifo->fifo, kFifoWatchKey);
            if (status != ZX_OK) {
              LOGF_ERROR("RxQueue::WatchThread port cancel failed %s",
                         zx_status_get_string(status));
              return status;
            }
            waiting_on_fifo = false;
          }
          observed_fifo = parent_->primary_rx_fifo();
          LOGF_TRACE("RxQueue primary FIFO changed, valid=%d", static_cast<bool>(observed_fifo));
          break;
        case kFifoWatchKey:
          if ((packet.signal.observed & ZX_FIFO_PEER_CLOSED) || packet.status != ZX_OK) {
            // If observing the FIFO fails, we're assuming that the session is being closed. We're
            // just going to dispose of our reference to the observed FIFO and wait for
            // `DeviceInterface` to signal us that a new primary session is available when that
            // happens.
            observed_fifo.reset();
            LOGF_TRACE("RxQueue fifo closed or bad status %s", zx_status_get_string(packet.status));
          } else {
            fifo_readable = true;
          }
          waiting_on_fifo = false;
          break;
        default:
          ZX_ASSERT_MSG(packet.key == kTriggerRxKey, "Unrecognized packet in rx queue");
          break;
      }

      size_t pushed = 0;
      bool should_wait_on_fifo;

      fbl::AutoLock rx_lock(&parent_->rx_lock());
      SharedAutoLock control_lock(&parent_->control_lock());
      size_t push_count = parent_->info().rx_depth - device_buffer_count_;
      if (parent_->IsDataPlaneOpen()) {
        for (; pushed < push_count; pushed++) {
          if (PrepareBuff(&space_buffers_[pushed]) != ZX_OK) {
            break;
          }
        }
      }

      if (fifo_readable && push_count == 0 && in_flight_->available()) {
        RxSessionTransaction transaction(this);
        parent_->LoadRxDescriptors(transaction);
      }
      // We only need to wait on the FIFO if we didn't get enough buffers.
      // Otherwise, we'll trigger the loop again once the device calls CompleteRx.
      //
      // Similarly, we should not wait on the FIFO if the device has not started yet.
      should_wait_on_fifo =
          device_buffer_count_ < parent_->info().rx_depth && parent_->IsDataPlaneOpen();

      // We release the main rx queue and control locks before calling into the parent device so we
      // don't cause a re-entrant deadlock.
      rx_lock.release();
      control_lock.release();

      if (pushed != 0) {
        parent_->QueueRxSpace(space_buffers_.get(), static_cast<uint32_t>(pushed));
      }

      // No point waiting in RX fifo if we filled the device buffers, we'll get a signal to wait
      // on the fifo later.
      if (should_wait_on_fifo) {
        if (!observed_fifo) {
          // This can happen if we get triggered to fetch more buffers, but the primary session is
          // already tearing down, it's fine to just proceed.
          LOG_TRACE("RxQueue::WatchThread Should wait but no FIFO is here");
        } else if (!waiting_on_fifo) {
          status = observed_fifo->fifo.wait_async(rx_watch_port_, kFifoWatchKey,
                                                  ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED, 0);
          if (status == ZX_OK) {
            waiting_on_fifo = true;
          } else {
            LOGF_ERROR("RxQueue::WatchThread wait_async failed: %s", zx_status_get_string(status));
            return status;
          }
        }
      }
    }
  };

  zx_status_t status = loop();
  if (status != ZX_OK) {
    LOGF_ERROR("network-device: RxQueue::WatchThread finished loop with error: %s",
               zx_status_get_string(status));
  }
  LOG_TRACE("network-device: watch thread done");
  return 0;
}

uint32_t RxQueue::SessionTransaction::remaining() __TA_REQUIRES(queue_->parent_->rx_lock()) {
  // NB: __TA_REQUIRES here is just encoding that a SessionTransaction always holds a lock for
  // its parent queue, the protection from misuse comes from the annotations on
  // `SessionTransaction`'s constructor and destructor.
  return queue_->in_flight_->available();
}

void RxQueue::SessionTransaction::Push(Session* session, uint16_t descriptor)
    __TA_REQUIRES(queue_->parent_->rx_lock()) {
  // NB: __TA_REQUIRES here is just encoding that a SessionTransaction always holds a lock for
  // its parent queue, the protection from misuse comes from the annotations on
  // `SessionTransaction`'s constructor and destructor.
  uint32_t idx = queue_->in_flight_->Push(InFlightBuffer(session, descriptor));
  queue_->available_queue_->Push(idx);
}

}  // namespace network::internal
