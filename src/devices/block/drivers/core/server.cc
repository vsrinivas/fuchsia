// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "server.h"

#include <lib/zx/fifo.h>
#include <string.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <limits>
#include <new>
#include <optional>
#include <utility>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/block.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>

#include "message-group.h"

namespace {

// This signal is set on the FIFO when the server should be instructed
// to terminate.
constexpr zx_signals_t kSignalFifoTerminate = ZX_USER_SIGNAL_0;
// This signal is set on the FIFO when, after the thread enqueueing operations
// has encountered a barrier, all prior operations have completed.
constexpr zx_signals_t kSignalFifoOpsComplete = ZX_USER_SIGNAL_1;
// Signalled on the fifo when it has finished terminating.
// (If we need to free up user signals, this could easily be transformed
// into a completion object).
constexpr zx_signals_t kSignalFifoTerminated = ZX_USER_SIGNAL_2;

void BlockCompleteCb(void* cookie, zx_status_t status, block_op_t* bop) {
  ZX_DEBUG_ASSERT(bop != nullptr);
  std::unique_ptr<Message> msg(static_cast<Message*>(cookie));
  msg->set_result(status);
  msg->Complete();
  msg.reset();
}

uint32_t OpcodeToCommand(uint32_t opcode) {
  // TODO(fxbug.dev/31695): Unify block protocol and block device interface
  static_assert(BLOCK_OP_READ == BLOCKIO_READ, "");
  static_assert(BLOCK_OP_WRITE == BLOCKIO_WRITE, "");
  static_assert(BLOCK_OP_FLUSH == BLOCKIO_FLUSH, "");
  static_assert(BLOCK_OP_TRIM == BLOCKIO_TRIM, "");
  static_assert(BLOCK_FL_BARRIER_BEFORE == BLOCKIO_BARRIER_BEFORE, "");
  static_assert(BLOCK_FL_BARRIER_AFTER == BLOCKIO_BARRIER_AFTER, "");
  const uint32_t shared = BLOCK_OP_MASK | BLOCK_FL_BARRIER_BEFORE | BLOCK_FL_BARRIER_AFTER;
  return opcode & shared;
}

void InQueueAdd(zx_handle_t vmo, uint64_t length, uint64_t vmo_offset, uint64_t dev_offset,
                Message* msg, MessageQueue* queue) {
  block_op_t* bop = msg->Op();
  bop->rw.length = (uint32_t)length;
  bop->rw.vmo = vmo;
  bop->rw.offset_dev = dev_offset;
  bop->rw.offset_vmo = vmo_offset;
  queue->push_back(msg);
}

}  // namespace

void Server::BarrierComplete() {
  // This is the only location that unsets the OpsComplete
  // signal. We'll never "miss" a signal, because we process
  // the queue AFTER unsetting it.
  barrier_in_progress_.store(false);
  fifo_.signal(kSignalFifoOpsComplete, 0);
  InQueueDrainer();
}

void Server::TerminateQueue() {
  InQueueDrainer();
  while (true) {
    if (pending_count_.load() == 0 && in_queue_.is_empty()) {
      return;
    }
    zx_signals_t signals = kSignalFifoOpsComplete;
    zx_signals_t seen = 0;
    fifo_.wait_one(signals, zx::deadline_after(zx::msec(10)), &seen);
    if (seen & kSignalFifoOpsComplete) {
      BarrierComplete();
    }
  }
}

void Server::SendResponse(const block_fifo_response_t& response) {
  for (;;) {
    zx_status_t status = fifo_.write_one(response);
    switch (status) {
      case ZX_OK:
        return;
      case ZX_ERR_SHOULD_WAIT: {
        zx_signals_t signals;
        status = zx_object_wait_one(fifo_.get_handle(),
                                    ZX_FIFO_WRITABLE | ZX_FIFO_PEER_CLOSED | kSignalFifoTerminate,
                                    ZX_TIME_INFINITE, &signals);
        if (status != ZX_OK) {
          zxlogf(WARNING, "(fifo) zx_object_wait_one failed: %s", zx_status_get_string(status));
          return;
        }
        if (signals & kSignalFifoTerminate) {
          // The server is shutting down and we shouldn't block, so dump the response and return.
          return;
        }
        break;
      }
      default:
        zxlogf(WARNING, "Fifo write failed: %s", zx_status_get_string(status));
        return;
    }
  }
}

void Server::FinishTransaction(zx_status_t status, reqid_t reqid, groupid_t group) {
  if (group != kNoGroup) {
    groups_[group]->Complete(status);
  } else {
    SendResponse(block_fifo_response_t{
        .status = status,
        .reqid = reqid,
        .group = group,
        .count = 1,
    });
  }
}

zx_status_t Server::Read(block_fifo_request_t* requests, size_t* count) {
  auto cleanup = fbl::MakeAutoCall([this]() {
    TerminateQueue();
    ZX_ASSERT(pending_count_.load() == 0);
    ZX_ASSERT(in_queue_.is_empty());
    fifo_.signal(0, kSignalFifoTerminated);
  });

  // Keep trying to read messages from the fifo until we have a reason to
  // terminate
  zx_status_t status;
  while (true) {
    status = fifo_.read(requests, BLOCK_FIFO_MAX_DEPTH, count);
    zx_signals_t signals;
    zx_signals_t seen;
    switch (status) {
      case ZX_ERR_SHOULD_WAIT:
        signals =
            ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED | kSignalFifoTerminate | kSignalFifoOpsComplete;
        if ((status = fifo_.wait_one(signals, zx::time::infinite(), &seen)) != ZX_OK) {
          return status;
        }
        if (seen & kSignalFifoOpsComplete) {
          BarrierComplete();
          continue;
        }
        if ((seen & ZX_FIFO_PEER_CLOSED) || (seen & kSignalFifoTerminate)) {
          return ZX_ERR_PEER_CLOSED;
        }
        // Try reading again...
        break;
      case ZX_OK:
        cleanup.cancel();
        return ZX_OK;
      default:
        return status;
    }
  }
}

zx_status_t Server::FindVmoIdLocked(vmoid_t* out) {
  for (vmoid_t i = last_id_; i < std::numeric_limits<vmoid_t>::max(); i++) {
    if (!tree_.find(i).IsValid()) {
      *out = i;
      last_id_ = static_cast<vmoid_t>(i + 1);
      return ZX_OK;
    }
  }
  for (vmoid_t i = BLOCK_VMOID_INVALID + 1; i < last_id_; i++) {
    if (!tree_.find(i).IsValid()) {
      *out = i;
      last_id_ = static_cast<vmoid_t>(i + 1);
      return ZX_OK;
    }
  }
  return ZX_ERR_NO_RESOURCES;
}

zx_status_t Server::AttachVmo(zx::vmo vmo, vmoid_t* out) {
  zx_status_t status;
  vmoid_t id;
  fbl::AutoLock server_lock(&server_lock_);
  if ((status = FindVmoIdLocked(&id)) != ZX_OK) {
    return status;
  }

  fbl::AllocChecker ac;
  fbl::RefPtr<IoBuffer> ibuf = fbl::AdoptRef(new (&ac) IoBuffer(std::move(vmo), id));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  tree_.insert(std::move(ibuf));
  *out = id;
  return ZX_OK;
}

void Server::TxnEnd() {
  size_t old_count = pending_count_.fetch_sub(1);
  ZX_ASSERT(old_count > 0);
  if ((old_count == 1) && barrier_in_progress_.load()) {
    // Since we're avoiding locking, and there is a gap between
    // "pending count decremented" and "FIFO signalled", it's possible
    // that we'll receive spurious wakeup requests.
    fifo_.signal(0, kSignalFifoOpsComplete);
  }
}

void Server::InQueueDrainer() {
  while (true) {
    if (in_queue_.is_empty()) {
      return;
    }

    auto msg = in_queue_.begin();
    block_op_t* op = msg->Op();
    if (deferred_barrier_before_) {
      op->command |= BLOCK_FL_BARRIER_BEFORE;
      deferred_barrier_before_ = false;
    }

    if (op->command & BLOCK_FL_BARRIER_BEFORE) {
      barrier_in_progress_.store(true);
      if (pending_count_.load() > 0) {
        return;
      }
      // Since we're the only thread that could add to pending
      // count, we reliably know it has terminated.
      barrier_in_progress_.store(false);
    }
    if (op->command & BLOCK_FL_BARRIER_AFTER) {
      deferred_barrier_before_ = true;
    }
    pending_count_.fetch_add(1);
    in_queue_.pop_front();
    // Underlying block device drivers should not see block barriers
    // which are already handled by the block midlayer.
    //
    // This may be altered in the future if block devices
    // are capable of implementing hardware barriers.
    op->command &= ~(BLOCK_FL_BARRIER_BEFORE | BLOCK_FL_BARRIER_AFTER);
    bp_->Queue(op, BlockCompleteCb, &*msg);
  }
}

zx_status_t Server::Create(ddk::BlockProtocolClient* bp,
                           fzl::fifo<block_fifo_request_t, block_fifo_response_t>* fifo_out,
                           std::unique_ptr<Server>* out) {
  fbl::AllocChecker ac;
  std::unique_ptr<Server> bs(new (&ac) Server(bp));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status;
  if ((status = fzl::create_fifo(BLOCK_FIFO_MAX_DEPTH, 0, fifo_out, &bs->fifo_)) != ZX_OK) {
    return status;
  }

  for (size_t i = 0; i < std::size(bs->groups_); i++) {
    bs->groups_[i] = std::make_unique<MessageGroup>(*bs, static_cast<groupid_t>(i));
  }

  // Notably, drop ZX_RIGHT_SIGNAL_PEER, since we use bs->fifo for thread
  // signalling internally within the block server.
  zx_rights_t rights =
      ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_SIGNAL | ZX_RIGHT_WAIT;
  if ((status = fifo_out->replace(rights, fifo_out)) != ZX_OK) {
    return status;
  }

  bp->Query(&bs->info_, &bs->block_op_size_);

  // TODO(fxbug.dev/31467): Allocate BlockMsg arena based on block_op_size_.

  *out = std::move(bs);
  return ZX_OK;
}

zx_status_t Server::ProcessReadWriteRequest(block_fifo_request_t* request) {
  // TODO(fxbug.dev/31470): Reduce the usage of this lock (only used to protect
  // IoBuffers).
  fbl::AutoLock server_lock(&server_lock_);

  auto iobuf = tree_.find(request->vmoid);
  if (!iobuf.IsValid()) {
    // Operation which is not accessing a valid vmo.
    zxlogf(WARNING, "ProcessReadWriteRequest: vmoid %d is not valid, failing request",
           request->vmoid);
    return ZX_ERR_IO;
  }

  if (!request->length) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Hack to ensure that the vmo is valid.
  // In the future, this code will be responsible for pinning VMO pages,
  // and the completion will be responsible for un-pinning those same pages.
  uint32_t bsz = info_.block_size;
  zx_status_t status = iobuf->ValidateVmoHack(bsz * request->length, bsz * request->vmo_offset);
  if (status != ZX_OK) {
    return status;
  }

  std::unique_ptr<Message> msg;

  const uint32_t max_xfer = info_.max_transfer_size / bsz;
  if (max_xfer != 0 && max_xfer < request->length) {
    // If the request is larger than the maximum transfer size,
    // split it up into a collection of smaller block messages.
    uint32_t len_remaining = request->length;
    uint64_t vmo_offset = request->vmo_offset;
    uint64_t dev_offset = request->dev_offset;
    uint32_t sub_txns = fbl::round_up(len_remaining, max_xfer) / max_xfer;

    // For groups, we simply add extra (uncounted) messages to the existing MessageGroup,
    // but for ungrouped messages we create a oneshot MessageGroup.
    std::unique_ptr<MessageGroup> oneshot_group = nullptr;
    MessageGroup* transaction_group = nullptr;

    if (request->group == kNoGroup) {
      oneshot_group = std::make_unique<MessageGroup>(*this);
      oneshot_group->ExpectResponses(sub_txns, 1, request->reqid);
      transaction_group = oneshot_group.get();
    } else {
      groups_[request->group]->ExpectResponses(sub_txns - 1, 0, std::nullopt);
      transaction_group = groups_[request->group].get();
    }

    // Once all of these smaller messages are created, splice
    // them into the input queue together.
    MessageQueue sub_txns_queue;

    uint32_t sub_txn_idx = 0;

    auto completer = [transaction_group](zx_status_t status, block_fifo_request_t& req) {
      transaction_group->Complete(status);
    };
    while (sub_txn_idx != sub_txns) {
      // We'll be using a new BlockMsg for each sub-component.
      if (msg == nullptr) {
        status =
            Message::Create(iobuf.CopyPointer(), this, request, block_op_size_, completer, &msg);
        if (status != ZX_OK) {
          return status;
        }
        msg->Op()->command = OpcodeToCommand(request->opcode);
      }

      uint32_t length = std::min(len_remaining, max_xfer);
      len_remaining -= length;

      // Only set the "AFTER" barrier on the last sub-txn.
      msg->Op()->command &= ~(sub_txn_idx == sub_txns - 1 ? 0 : BLOCK_FL_BARRIER_AFTER);
      // Only set the "BEFORE" barrier on the first sub-txn.
      msg->Op()->command &= ~(sub_txn_idx == 0 ? 0 : BLOCK_FL_BARRIER_BEFORE);
      InQueueAdd(iobuf->vmo(), length, vmo_offset, dev_offset, msg.release(), &sub_txns_queue);
      vmo_offset += length;
      dev_offset += length;
      sub_txn_idx++;
    }
    ZX_DEBUG_ASSERT(len_remaining == 0);

    if (oneshot_group) {
      // Release the oneshot MessageGroup: it will free itself once all messages have been handled.
      oneshot_group.release();
    }
    in_queue_.splice(in_queue_.end(), sub_txns_queue);
  } else {
    auto completer = [this](zx_status_t status, block_fifo_request_t& req) {
      FinishTransaction(status, req.reqid, req.group);
    };
    status = Message::Create(iobuf.CopyPointer(), this, request, block_op_size_,
                             std::move(completer), &msg);
    if (status != ZX_OK) {
      return status;
    }

    msg->Op()->command = OpcodeToCommand(request->opcode);

    InQueueAdd(iobuf->vmo(), request->length, request->vmo_offset, request->dev_offset,
               msg.release(), &in_queue_);
  }
  return ZX_OK;
}

zx_status_t Server::ProcessCloseVmoRequest(block_fifo_request_t* request) {
  fbl::AutoLock server_lock(&server_lock_);

  auto iobuf = tree_.find(request->vmoid);
  if (!iobuf.IsValid()) {
    // Operation which is not accessing a valid vmo
    zxlogf(WARNING, "ProcessCloseVmoRequest: vmoid %d is not valid, failing request",
           request->vmoid);
    return ZX_ERR_IO;
  }

  // TODO(smklein): Ensure that "iobuf" is not being used by
  // any in-flight txns.
  tree_.erase(*iobuf);
  return ZX_OK;
}

zx_status_t Server::ProcessFlushRequest(block_fifo_request_t* request) {
  std::unique_ptr<Message> msg;
  auto completer = [this](zx_status_t result, block_fifo_request_t& req) {
    FinishTransaction(result, req.reqid, req.group);
  };
  zx_status_t status =
      Message::Create(nullptr, this, request, block_op_size_, std::move(completer), &msg);
  if (status != ZX_OK) {
    return status;
  }
  msg->Op()->command = OpcodeToCommand(request->opcode);
  InQueueAdd(ZX_HANDLE_INVALID, 0, 0, 0, msg.release(), &in_queue_);
  return ZX_OK;
}

zx_status_t Server::ProcessTrimRequest(block_fifo_request_t* request) {
  if (!request->length) {
    return ZX_ERR_INVALID_ARGS;
  }

  std::unique_ptr<Message> msg;
  auto completer = [this](zx_status_t result, block_fifo_request_t& req) {
    FinishTransaction(result, req.reqid, req.group);
  };
  zx_status_t status =
      Message::Create(nullptr, this, request, block_op_size_, std::move(completer), &msg);
  if (status != ZX_OK) {
    return status;
  }
  msg->Op()->command = OpcodeToCommand(request->opcode);
  msg->Op()->trim.length = request->length;
  msg->Op()->trim.offset_dev = request->dev_offset;
  in_queue_.push_back(msg.release());
  return ZX_OK;
}

void Server::ProcessRequest(block_fifo_request_t* request) {
  zx_status_t status;
  switch (request->opcode & BLOCKIO_OP_MASK) {
    case BLOCKIO_READ:
    case BLOCKIO_WRITE:
      if ((status = ProcessReadWriteRequest(request)) != ZX_OK) {
        FinishTransaction(status, request->reqid, request->group);
      }
      break;
    case BLOCKIO_FLUSH:
      if ((status = ProcessFlushRequest(request)) != ZX_OK) {
        FinishTransaction(status, request->reqid, request->group);
      }
      break;
    case BLOCKIO_TRIM:
      if ((status = ProcessTrimRequest(request)) != ZX_OK) {
        FinishTransaction(status, request->reqid, request->group);
      }
      break;
    case BLOCKIO_CLOSE_VMO:
      status = ProcessCloseVmoRequest(request);
      FinishTransaction(status, request->reqid, request->group);
      break;
    default:
      zxlogf(WARNING, "Unrecognized block server operation: %d", request->opcode);
      FinishTransaction(ZX_ERR_NOT_SUPPORTED, request->reqid, request->group);
  }
}

zx_status_t Server::Serve() {
  zx_status_t status;
  block_fifo_request_t requests[BLOCK_FIFO_MAX_DEPTH];
  size_t count;
  while (true) {
    // Attempt to drain as much of the input queue as possible
    // before (potentially) blocking in Read.
    InQueueDrainer();

    if ((status = Read(requests, &count) != ZX_OK)) {
      return status;
    }

    for (size_t i = 0; i < count; i++) {
      bool wants_reply = requests[i].opcode & BLOCKIO_GROUP_LAST;
      bool use_group = requests[i].opcode & BLOCKIO_GROUP_ITEM;

      reqid_t reqid = requests[i].reqid;

      if (use_group) {
        groupid_t group = requests[i].group;
        if (group >= MAX_TXN_GROUP_COUNT) {
          // Operation which is not accessing a valid group.
          zxlogf(WARNING, "Serve: group %d is not valid, failing request", group);
          if (wants_reply) {
            FinishTransaction(ZX_ERR_IO, reqid, group);
          }
          continue;
        }

        // Enqueue the message against the transaction group.
        status = groups_[group]->ExpectResponses(1, 1,
                                                 wants_reply ? std::optional{reqid} : std::nullopt);
        if (status != ZX_OK) {
          zxlogf(WARNING, "Serve: Enqueue for group %d failed: %s", group,
                 zx_status_get_string(status));
          FinishTransaction(status, reqid, group);
          continue;
        }

      } else {
        requests[i].group = kNoGroup;
      }

      ProcessRequest(&requests[i]);
    }
  }
}

Server::Server(ddk::BlockProtocolClient* bp)
    : bp_(bp),
      block_op_size_(0),
      pending_count_(0),
      barrier_in_progress_(false),
      last_id_(BLOCK_VMOID_INVALID + 1) {
  size_t block_op_size;
  bp->Query(&info_, &block_op_size);
}

Server::~Server() {
  ZX_ASSERT(pending_count_.load() == 0);
  ZX_ASSERT(in_queue_.is_empty());
}

void Server::Shutdown() {
  // Identify that the server should stop reading and return,
  // implicitly closing the fifo.
  fifo_.signal(0, kSignalFifoTerminate);
  zx_signals_t signals = kSignalFifoTerminated;
  zx_signals_t seen;
  fifo_.wait_one(signals, zx::time::infinite(), &seen);
}

bool Server::WillTerminate() const {
  zx_signals_t signals;
  return fifo_.wait_one(ZX_FIFO_PEER_CLOSED, zx::time::infinite_past(), &signals) == ZX_OK;
}
