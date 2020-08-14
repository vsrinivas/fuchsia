// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rx_queue.h"

#include <zircon/assert.h>

#include "device_interface.h"
#include "log.h"

namespace network::internal {

namespace {
constexpr uint64_t kTriggerRxKey = 1;
constexpr uint64_t kSessionSwitchKey = 2;
constexpr uint64_t kFifoWatchKey = 3;
constexpr uint64_t kQuitWatchKey = 4;
}  // namespace

RxQueue::~RxQueue() {
  // running_ is tied to the lifetime of the watch thread, it's cleared in`RxQueue::JoinThread`.
  // This assertion protects us from destruction paths where `RxQueue::JoinThread` is not called.
  ZX_ASSERT_MSG(!running_, "RxQueue destroyed without disposing of port and thread first.");
}

zx_status_t RxQueue::Create(DeviceInterface* parent, std::unique_ptr<RxQueue>* out) {
  fbl::AllocChecker ac;
  std::unique_ptr<RxQueue> queue(new (&ac) RxQueue(parent));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  fbl::AutoLock lock(&queue->lock_);
  // The RxQueue's capacity is the device's FIFO rx depth as opposed to the hardware's queue depth
  // so we can (possibly) reduce the amount of reads on the rx fifo during rx interrupts.
  auto capacity = parent->rx_fifo_depth();
  zx_status_t status;
  if ((status = IndexedSlab<InFlightBuffer>::Create(capacity, &queue->in_flight_)) != ZX_OK) {
    return status;
  }
  if ((status = RingQueue<uint32_t>::Create(capacity, &queue->available_queue_)) != ZX_OK) {
    return status;
  }

  auto device_depth = parent->info().rx_depth;

  queue->space_buffers_.reset(new (&ac) rx_space_buffer_t[device_depth]);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  queue->buffer_parts_.reset(new (&ac) BufferParts[device_depth]);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  for (uint32_t i = 0; i < device_depth; i++) {
    queue->space_buffers_[i].data.parts_list = queue->buffer_parts_[i].data();
  }

  if ((status = zx::port::create(0, &queue->rx_watch_port_)) != ZX_OK) {
    LOGF_ERROR("network-device: failed to create rx watch port: %s", zx_status_get_string(status));
    return status;
  }

  thrd_t watch_thread;
  if (thrd_create_with_name(
          &watch_thread, [](void* ctx) { return reinterpret_cast<RxQueue*>(ctx)->WatchThread(); },
          reinterpret_cast<void*>(queue.get()), "netdevice:rx_watch") != thrd_success) {
    LOG_ERROR("network-device: rx queue failed to create thread");
    return ZX_ERR_INTERNAL;
  }
  queue->rx_watch_thread_ = watch_thread;
  queue->running_ = true;
  *out = std::move(queue);
  return ZX_OK;
}

void RxQueue::TriggerRxWatch() {
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
    thrd_join(*rx_watch_thread_, nullptr);
    // Dispose of the port and the thread handle.
    rx_watch_port_.reset();
    rx_watch_thread_.reset();
  }
}

void RxQueue::PurgeSession(Session* session) {
  fbl::AutoLock lock(&lock_);
  // Get rid of all available buffers that belong to the session and stop its rx path.
  session->StopRx();
  for (auto nu = available_queue_->count(); nu > 0; nu--) {
    auto b = available_queue_->Pop();
    if (in_flight_->Get(b).session == session) {
      in_flight_->Free(b);
    } else {
      // Push back to the end of the queue.
      available_queue_->Push(b);
    }
  }
}

void RxQueue::Reclaim() {
  for (auto i = in_flight_->begin(); i != in_flight_->end(); ++i) {
    auto& buff = in_flight_->Get(*i);
    // reclaim the buffer if the device has it
    if (buff.flags & kDeviceHasBuffer) {
      ReclaimBuffer(*i);
    }
  }
}

void RxQueue::ReclaimBuffer(uint32_t id) {
  auto& buff = in_flight_->Get(id);
  if (buff.session->RxReturned()) {
    buff.flags &= static_cast<uint16_t>(~kDeviceHasBuffer);
    available_queue_->Push(id);
  } else {
    in_flight_->Free(id);
  }
  device_buffer_count_--;
}

std::tuple<RxQueue::InFlightBuffer*, uint32_t> RxQueue::GetBuffer() {
  if (available_queue_->count() != 0) {
    auto idx = available_queue_->Pop();
    return std::make_tuple(&in_flight_->Get(idx), idx);
  }
  // need to fetch more from the session
  if (in_flight_->available() == 0) {
    // no more space to keep in flight buffers
    LOG_ERROR("network-device: can't fit more in-flight buffers");
    return std::make_tuple(nullptr, 0);
  }

  SessionTransaction transaction(this);
  if (parent_->LoadRxDescriptors(&transaction) != ZX_OK) {
    // failed to load descriptors from session.
    return std::make_tuple(nullptr, 0);
  }
  // LoadRxDescriptors can't return OK if it couldn't load any descriptors.
  auto idx = available_queue_->Pop();
  return std::make_tuple(&in_flight_->Get(idx), idx);
}

zx_status_t RxQueue::PrepareBuff(rx_space_buffer_t* buff) {
  auto res = GetBuffer();
  auto session_buffer = std::get<0>(res);
  auto index = std::get<1>(res);
  if (session_buffer == nullptr) {
    return ZX_ERR_NO_RESOURCES;
  }
  zx_status_t status;
  buff->id = index;
  if ((status = session_buffer->session->FillRxSpace(session_buffer->descriptor_index, buff)) !=
      ZX_OK) {
    // if the session can't fill Rx for any reason, kill it.
    session_buffer->session->Kill();
    // put the index back at the end of the available queue
    available_queue_->Push(index);
    return status;
  }

  // mark that this buffer belongs to implementation.
  session_buffer->flags |= kDeviceHasBuffer;
  session_buffer->session->RxTaken();
  device_buffer_count_++;
  return ZX_OK;
}

void RxQueue::CompleteRxList(const rx_buffer_t* rx, size_t count) {
  fbl::AutoLock lock(&lock_);
  device_buffer_count_ -= count;
  while (count--) {
    auto& session_buffer = in_flight_->Get(rx->id);
    // mark that the buffer does not belong to device anymore:
    session_buffer.flags &= static_cast<uint16_t>(~kDeviceHasBuffer);
    session_buffer.session->RxReturned();
    if (session_buffer.session->CompleteRx(session_buffer.descriptor_index, rx)) {
      // we can reuse the descriptor
      available_queue_->Push(rx->id);
    } else {
      in_flight_->Free(rx->id);
    }
    rx++;
  }
  ReturnBuffers();
  if (device_buffer_count_ <= parent_->rx_notify_threshold()) {
    TriggerRxWatch();
  }
}

void RxQueue::ReturnBuffers() { parent_->CommitAllSessions(); }

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
          }
          observed_fifo = parent_->primary_rx_fifo();
          LOGF_TRACE("RxQueue primary FIFO changed, valid=%d", static_cast<bool>(observed_fifo));
          break;
        case kFifoWatchKey:
          if ((packet.signal.observed & ZX_FIFO_PEER_CLOSED) || packet.status != ZX_OK) {
            // If observing the FIFO fails, we're assuming that the sesions is being closed. We're
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

      fbl::AutoLock lock(&lock_);
      size_t push_count = parent_->info().rx_depth - device_buffer_count_;
      if (parent_->IsDataPlaneOpen()) {
        for (; pushed < push_count; pushed++) {
          if (PrepareBuff(&space_buffers_[pushed]) != ZX_OK) {
            break;
          }
        }
      }

      if (fifo_readable && push_count == 0 && in_flight_->available()) {
        SessionTransaction transaction(this);
        parent_->LoadRxDescriptors(&transaction);
      }
      // We only need to wait on the FIFO if we didn't get enough buffers.
      // Otherwise, we'll trigger the loop again once the device calls CompleteRx.
      should_wait_on_fifo = device_buffer_count_ < parent_->info().rx_depth;

      // We release the main rx queue lock before calling into the parent device,
      // so we don't cause a re-entrant deadlock.
      // We keep the buffers_lock_ since those are tied to our lifecycle.
      lock.release();

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

uint32_t RxQueue::SessionTransaction::remaining() __TA_REQUIRES(queue_->lock_) {
  // NB: __TA_REQUIRES here is just encoding that a SessionTransaction always holds a lock for
  // its parent queue, the protection from misuse comes from the annotations on
  // `SessionTransaction`'s constructor and destructor.
  return queue_->in_flight_->available();
}

void RxQueue::SessionTransaction::Push(Session* session, uint16_t descriptor)
    __TA_REQUIRES(queue_->lock_) {
  // NB: __TA_REQUIRES here is just encoding that a SessionTransaction always holds a lock for
  // its parent queue, the protection from misuse comes from the annotations on
  // `SessionTransaction`'s constructor and destructor.
  uint32_t idx = queue_->in_flight_->Push(InFlightBuffer(session, descriptor));
  queue_->available_queue_->Push(idx);
}

}  // namespace network::internal
