// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "server.h"

#include <fuchsia/hardware/block/c/banjo.h>
#include <lib/zx/fifo.h>
#include <string.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <limits>
#include <new>
#include <optional>
#include <utility>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>

#include "message-group.h"
#include "src/devices/lib/block/block.h"

namespace {

// This signal is set on the FIFO when the server should be instructed
// to terminate.
constexpr zx_signals_t kSignalFifoTerminate = ZX_USER_SIGNAL_0;

void BlockCompleteCb(void* cookie, zx_status_t status, block_op_t* bop) {
  ZX_DEBUG_ASSERT(bop != nullptr);
  std::unique_ptr<Message> msg(static_cast<Message*>(cookie));
  msg->set_result(status);
  msg->Complete();
  msg.reset();
}

uint32_t OpcodeToCommand(uint32_t opcode) {
  const uint32_t shared = BLOCK_OP_MASK | BLOCK_FL_BARRIER_BEFORE | BLOCK_FL_BARRIER_AFTER;
  return opcode & shared;
}

}  // namespace

void Server::Enqueue(std::unique_ptr<Message> message) {
  {
    fbl::AutoLock server_lock(&server_lock_);
    ++pending_count_;
  }
  bp_->Queue(message->Op(), BlockCompleteCb, message.release());
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
  // Keep trying to read messages from the fifo until we have a reason to
  // terminate
  zx_status_t status;
  while (true) {
    status = fifo_.read(requests, BLOCK_FIFO_MAX_DEPTH, count);
    zx_signals_t signals;
    zx_signals_t seen;
    switch (status) {
      case ZX_ERR_SHOULD_WAIT:
        signals = ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED | kSignalFifoTerminate;
        if ((status = fifo_.wait_one(signals, zx::time::infinite(), &seen)) != ZX_OK) {
          return status;
        }
        if ((seen & ZX_FIFO_PEER_CLOSED) || (seen & kSignalFifoTerminate)) {
          return ZX_ERR_PEER_CLOSED;
        }
        // Try reading again...
        break;
      case ZX_OK:
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
  fbl::AutoLock lock(&server_lock_);
  // N.B. If pending_count_ hits zero, after dropping the lock the instance of Server can be
  // destroyed.
  if (--pending_count_ == 0) {
    condition_.Broadcast();
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
  fbl::RefPtr<IoBuffer> iobuf;
  {
    fbl::AutoLock lock(&server_lock_);
    auto iter = tree_.find(request->vmoid);
    if (!iter.IsValid()) {
      // Operation which is not accessing a valid vmo.
      zxlogf(WARNING, "ProcessReadWriteRequest: vmoid %d is not valid, failing request",
             request->vmoid);
      return ZX_ERR_IO;
    }
    iobuf = iter.CopyPointer();
  }

  if (!request->length) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Hack to ensure that the vmo is valid.
  // In the future, this code will be responsible for pinning VMO pages,
  // and the completion will be responsible for un-pinning those same pages.
  uint64_t bsz = info_.block_size;
  zx_status_t status = iobuf->ValidateVmoHack(bsz * request->length, bsz * request->vmo_offset);
  if (status != ZX_OK) {
    return status;
  }

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
      ZX_ASSERT(oneshot_group->ExpectResponses(sub_txns, 1, request->reqid) == ZX_OK);
      transaction_group = oneshot_group.get();
    } else {
      transaction_group = groups_[request->group].get();
      // If != ZX_OK, it means that we've just received a response to an earlier request that
      // failed.  It should happen rarely because we called ExpectedResponses just prior to this
      // function and it returned ZX_OK.  It's safe to continue at this point and just assume things
      // are OK; it's not worth trying to handle this as a special case.
      [[maybe_unused]] zx_status_t status =
          transaction_group->ExpectResponses(sub_txns - 1, 0, std::nullopt);
    }

    uint32_t sub_txn_idx = 0;

    auto completer = [transaction_group](zx_status_t status, block_fifo_request_t& req) {
      transaction_group->Complete(status);
    };
    while (sub_txn_idx != sub_txns) {
      // We'll be using a new BlockMsg for each sub-component.
      std::unique_ptr<Message> msg;
      if (zx_status_t status =
              Message::Create(iobuf, this, request, block_op_size_, completer, &msg);
          status != ZX_OK) {
        return status;
      }

      uint32_t length = std::min(len_remaining, max_xfer);
      len_remaining -= length;

      *msg->Op() = block_op{.rw = {
                                .command = OpcodeToCommand(request->opcode),
                                .vmo = iobuf->vmo(),
                                .length = length,
                                .offset_dev = dev_offset,
                                .offset_vmo = vmo_offset,
                            }};
      Enqueue(std::move(msg));
      vmo_offset += length;
      dev_offset += length;
      sub_txn_idx++;
    }
    ZX_DEBUG_ASSERT(len_remaining == 0);

    if (oneshot_group) {
      // Release the oneshot MessageGroup: it will free itself once all messages have been handled.
      oneshot_group.release();
    }
  } else {
    auto completer = [this](zx_status_t status, block_fifo_request_t& req) {
      FinishTransaction(status, req.reqid, req.group);
    };
    std::unique_ptr<Message> msg;
    if (zx_status_t status =
            Message::Create(iobuf, this, request, block_op_size_, std::move(completer), &msg);
        status != ZX_OK) {
      return status;
    }

    *msg->Op() = block_op{.rw = {
                              .command = OpcodeToCommand(request->opcode),
                              .vmo = iobuf->vmo(),
                              .length = request->length,
                              .offset_dev = request->dev_offset,
                              .offset_vmo = request->vmo_offset,
                          }};
    Enqueue(std::move(msg));
  }
  return ZX_OK;
}

zx_status_t Server::ProcessCloseVmoRequest(block_fifo_request_t* request) {
  fbl::AutoLock lock(&server_lock_);
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
  Enqueue(std::move(msg));
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
  Enqueue(std::move(msg));
  return ZX_OK;
}

void Server::ProcessRequest(block_fifo_request_t* request) {
  if (request->opcode & (BLOCK_FL_BARRIER_BEFORE | BLOCK_FL_BARRIER_AFTER)) {
    zxlogf(WARNING, "Barriers not supported");
    FinishTransaction(ZX_ERR_NOT_SUPPORTED, request->reqid, request->group);
    return;
  }
  switch (request->opcode & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE:
      if (zx_status_t status = ProcessReadWriteRequest(request); status != ZX_OK) {
        FinishTransaction(status, request->reqid, request->group);
      }
      break;
    case BLOCK_OP_FLUSH:
      if (zx_status_t status = ProcessFlushRequest(request); status != ZX_OK) {
        FinishTransaction(status, request->reqid, request->group);
      }
      break;
    case BLOCK_OP_TRIM:
      if (zx_status_t status = ProcessTrimRequest(request); status != ZX_OK) {
        FinishTransaction(status, request->reqid, request->group);
      }
      break;
    case BLOCK_OP_CLOSE_VMO:
      FinishTransaction(ProcessCloseVmoRequest(request), request->reqid, request->group);
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
    if ((status = Read(requests, &count) != ZX_OK)) {
      return status;
    }

    for (size_t i = 0; i < count; i++) {
      bool wants_reply = requests[i].opcode & BLOCK_GROUP_LAST;
      bool use_group = requests[i].opcode & BLOCK_GROUP_ITEM;

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
          // This can happen if an earlier request that has been submitted has already failed.
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
    : bp_(bp), block_op_size_(0), pending_count_(0), last_id_(BLOCK_VMOID_INVALID + 1) {
  size_t block_op_size;
  bp->Query(&info_, &block_op_size);
}

Server::~Server() {
  fbl::AutoLock lock(&server_lock_);
  while (pending_count_ > 0)
    condition_.Wait(&server_lock_);
}

void Server::Shutdown() { fifo_.signal(0, kSignalFifoTerminate); }

bool Server::WillTerminate() const {
  zx_signals_t signals;
  return fifo_.wait_one(ZX_FIFO_PEER_CLOSED, zx::time::infinite_past(), &signals) == ZX_OK;
}
