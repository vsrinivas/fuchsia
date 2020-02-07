// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tx_queue.h"

#include <zircon/assert.h>

#include "device_interface.h"
#include "log.h"

namespace network::internal {

TxQueue::SessionTransaction::~SessionTransaction() {
  // when we destroy a session transaction, we commit all the queued buffers to the device.
  // release queue lock first
  queue_->lock_.Release();
  if (queued_ != 0) {
    queue_->parent_->QueueTx(queue_->tx_buffers_.get(), queued_);
  }
  queue_->buffers_lock_.Release();
}

tx_buffer_t* TxQueue::SessionTransaction::GetBuffer() {
  ZX_ASSERT(available_ != 0);
  return &queue_->tx_buffers_[queued_];
}

void TxQueue::SessionTransaction::Push(uint16_t descriptor) {
  ZX_ASSERT(available_ != 0);
  session_->TxTaken();
  queue_->tx_buffers_[queued_].id = queue_->Enqueue(session_, descriptor);
  available_--;
  queued_++;
}

TxQueue::SessionTransaction::SessionTransaction(TxQueue* parent, Session* session)
    : queue_(parent), session_(session), queued_(0) {
  // only get available slots after lock is acquired:
  // 0 available slots if parent is not enabled.
  queue_->lock_.Acquire();
  queue_->buffers_lock_.Acquire();
  available_ = queue_->parent_->IsDataPlaneOpen() ? queue_->in_flight_->available() : 0;
}

zx_status_t TxQueue::Create(DeviceInterface* parent, std::unique_ptr<TxQueue>* out) {
  // The Tx queue capacity is based on the underlying device's tx queue capacity.
  auto capacity = parent->info().tx_depth;

  fbl::AllocChecker ac;
  std::unique_ptr<TxQueue> queue(new (&ac) TxQueue(parent));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  fbl::AutoLock lock(&queue->lock_);
  fbl::AutoLock buffer_lock(&queue->buffers_lock_);

  zx_status_t status;
  if ((status = RingQueue<uint32_t>::Create(capacity, &queue->return_queue_)) != ZX_OK) {
    return status;
  }
  if ((status = IndexedSlab<InFlightBuffer>::Create(capacity, &queue->in_flight_)) != ZX_OK) {
    return status;
  }

  queue->tx_buffers_.reset(new (&ac) tx_buffer_t[capacity]);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  if (parent->info().device_features & FEATURE_TX_VIRTUAL_MEMORY_BUFFER) {
    queue->virtual_mem_parts_.reset(new (&ac) VirtualMemParts[capacity]);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    for (uint32_t i = 0; i < capacity; i++) {
      queue->tx_buffers_[i].virtual_mem.parts_list = queue->virtual_mem_parts_[i].data();
    }
  } else {
    for (uint32_t i = 0; i < capacity; i++) {
      queue->tx_buffers_[i].virtual_mem.parts_list = nullptr;
    }
  }

  if (parent->info().device_features & FEATURE_TX_PHYSICAL_MEMORY_BUFFER) {
    queue->physical_mem_parts_.reset(new (&ac) PhysicalMemParts[capacity]);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    for (uint32_t i = 0; i < capacity; i++) {
      queue->tx_buffers_[i].physical_mem.parts_list = queue->physical_mem_parts_[i].data();
    }
  } else {
    for (uint32_t i = 0; i < capacity; i++) {
      queue->tx_buffers_[i].physical_mem.parts_list = nullptr;
    }
  }

  *out = std::move(queue);
  return ZX_OK;
}

uint32_t TxQueue::Enqueue(Session* session, uint16_t descriptor) {
  return in_flight_->Push(InFlightBuffer(session, ZX_OK, descriptor));
}

void TxQueue::MarkComplete(uint32_t id, zx_status_t status) {
  auto& buff = in_flight_->Get(id);
  buff.result = status;
  buff.session->TxReturned();
  return_queue_->Push(id);
}

void TxQueue::Reclaim() {
  for (auto i = in_flight_->begin(); i != in_flight_->end(); ++i) {
    MarkComplete(*i, ZX_ERR_UNAVAILABLE);
  }
  ReturnBuffers();
}

void TxQueue::ReturnBuffers() {
  uint32_t count = 0;

  uint16_t desc_buffer[kMaxFifoDepth];
  ZX_ASSERT(return_queue_->count() <= kMaxFifoDepth);

  uint16_t* descs = desc_buffer;
  Session* cur_session = nullptr;
  // record if the queue was full, meaning that sessions may have halted fetching tx frames due
  // to overruns:
  bool was_full = in_flight_->available() == 0;
  while (return_queue_->count()) {
    auto& buff = in_flight_->Get(return_queue_->Peek());
    if (cur_session != nullptr && buff.session != cur_session) {
      // dispatch accumulated to session. This should only happen if we have outstanding
      // return buffers from different sessions.
      cur_session->ReturnTxDescriptors(desc_buffer, count);
      count = 0;
      descs = desc_buffer;
    }
    cur_session = buff.session;
    cur_session->MarkTxReturnResult(buff.descriptor_index, buff.result);
    *descs++ = buff.descriptor_index;
    count++;
    // pop from return queue and free space in in_flight queue:
    in_flight_->Free(return_queue_->Pop());
  }

  if (cur_session && count != 0) {
    cur_session->ReturnTxDescriptors(desc_buffer, count);
  }

  if (was_full) {
    parent_->NotifyTxQueueAvailable();
  }
  parent_->PruneDeadSessions();
}

void TxQueue::CompleteTxList(const tx_result_t* tx, size_t count) {
  fbl::AutoLock lock(&lock_);
  while (count--) {
    MarkComplete(tx->id, tx->status);
    tx++;
  }
  ReturnBuffers();
}

}  // namespace network::internal
