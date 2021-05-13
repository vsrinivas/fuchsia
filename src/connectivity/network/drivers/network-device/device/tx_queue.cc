// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tx_queue.h"

#include <zircon/assert.h>

#include "device_interface.h"
#include "log.h"
#include "session.h"

namespace network::internal {

TxQueue::SessionTransaction::~SessionTransaction() {
  // when we destroy a session transaction, we commit all the queued buffers to the device.
  // release queue lock first
  queue_->parent_->tx_lock().Release();
  if (queued_ != 0) {
    queue_->parent_->QueueTx(queue_->tx_buffers_.get(), queued_);
  }
  queue_->parent_->tx_buffers_lock().Release();
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
  queue_->parent_->tx_lock().Acquire();
  queue_->parent_->tx_buffers_lock().Acquire();
  available_ = queue_->parent_->IsDataPlaneOpen() ? queue_->in_flight_->available() : 0;
}

zx::status<std::unique_ptr<TxQueue>> TxQueue::Create(DeviceInterface* parent) {
  // The Tx queue capacity is based on the underlying device's tx queue capacity.
  auto capacity = parent->info().tx_depth;

  fbl::AllocChecker ac;
  std::unique_ptr<TxQueue> queue(new (&ac) TxQueue(parent));
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  fbl::AutoLock lock(&queue->parent_->tx_lock());
  fbl::AutoLock buffer_lock(&queue->parent_->tx_buffers_lock());

  zx::status return_queue = RingQueue<uint32_t>::Create(capacity);
  if (return_queue.is_error()) {
    return return_queue.take_error();
  }
  queue->return_queue_ = std::move(return_queue.value());

  zx::status in_flight = IndexedSlab<InFlightBuffer>::Create(capacity);
  if (in_flight.is_error()) {
    return in_flight.take_error();
  }
  queue->in_flight_ = std::move(in_flight.value());

  queue->tx_buffers_.reset(new (&ac) tx_buffer_t[capacity]);
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  queue->buffer_parts_.reset(new (&ac) BufferParts<buffer_region_t>[capacity]);
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  for (uint32_t i = 0; i < capacity; i++) {
    queue->tx_buffers_[i].data_list = queue->buffer_parts_[i].data();
  }

  return zx::ok(std::move(queue));
}

uint32_t TxQueue::Enqueue(Session* session, uint16_t descriptor) {
  return in_flight_->Push(InFlightBuffer(session, ZX_OK, descriptor));
}

bool TxQueue::ReturnBuffers() {
  size_t count = 0;

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

  return was_full;
}

void TxQueue::CompleteTxList(const tx_result_t* tx, size_t count) {
  fbl::AutoLock lock(&parent_->tx_lock());
  while (count--) {
    InFlightBuffer& buff = in_flight_->Get(tx->id);
    buff.result = tx->status;
    return_queue_->Push(tx->id);
    tx++;
  }
  bool was_full = ReturnBuffers();
  parent_->NotifyTxReturned(was_full);
}

}  // namespace network::internal
