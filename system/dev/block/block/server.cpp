// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <stdbool.h>
#include <string.h>

#include <ddk/device.h>
#include <ddk/protocol/block.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/limits.h>
#include <fbl/new.h>
#include <fbl/ref_ptr.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/syscalls.h>
#include <lib/zx/fifo.h>

#include "server.h"

namespace {

// This signal is set on the FIFO when the server should be instructed
// to terminate.
constexpr zx_signals_t kSignalFifoTerminate   = ZX_USER_SIGNAL_0;
// This signal is set on the FIFO when, after the thread enqueueing operations
// has encountered a barrier, all prior operations have completed.
constexpr zx_signals_t kSignalFifoOpsComplete = ZX_USER_SIGNAL_1;
// Signalled on the fifo when it has finished terminating.
// (If we need to free up user signals, this could easily be transformed
// into a completion object).
constexpr zx_signals_t kSignalFifoTerminated  = ZX_USER_SIGNAL_2;

void OutOfBandRespond(const zx::fifo& fifo, zx_status_t status, txnid_t txnid) {
    block_fifo_response_t response;
    response.status = status;
    response.txnid = txnid;
    response.count = 0;

    uint32_t actual;
    status = fifo.write_old(&response, sizeof(block_fifo_response_t), &actual);
    if (status != ZX_OK) {
        fprintf(stderr, "Block Server I/O error: Could not write response\n");
    }
}

void BlockComplete(BlockMsg* msg, zx_status_t status) {
    auto extra = msg->extra();
    // Since iobuf is a RefPtr, it lives at least as long as the txn,
    // and is not discarded underneath the block device driver.
    extra->iobuf = nullptr;
    extra->server->TxnEnd();
    extra->txn->Complete(status);
}

void BlockCompleteCb(block_op_t* bop, zx_status_t status) {
    ZX_DEBUG_ASSERT(bop != nullptr);
    BlockMsg msg(static_cast<block_msg_t*>(bop->cookie));
    BlockComplete(&msg, status);
}

uint32_t OpcodeToCommand(uint32_t opcode) {
    // TODO(ZX-1826): Unify block protocol and block device interface
    static_assert(BLOCK_OP_READ == BLOCKIO_READ, "");
    static_assert(BLOCK_OP_WRITE == BLOCKIO_WRITE, "");
    static_assert(BLOCK_OP_FLUSH == BLOCKIO_FLUSH, "");
    static_assert(BLOCK_FL_BARRIER_BEFORE == BLOCKIO_BARRIER_BEFORE, "");
    static_assert(BLOCK_FL_BARRIER_AFTER == BLOCKIO_BARRIER_AFTER, "");
    const uint32_t shared = BLOCK_OP_READ | BLOCK_OP_WRITE | BLOCK_OP_FLUSH |
            BLOCK_FL_BARRIER_BEFORE | BLOCK_FL_BARRIER_AFTER;
    return opcode & shared;
}

void InQueueAdd(zx_handle_t vmo, uint64_t length, uint64_t vmo_offset,
                uint64_t dev_offset, block_msg_t* msg, BlockMsgQueue* queue) {
    block_op_t* bop = &msg->op;
    bop->rw.length = (uint32_t) length;
    bop->rw.vmo = vmo;
    bop->rw.offset_dev = dev_offset;
    bop->rw.offset_vmo = vmo_offset;
    bop->rw.pages = NULL;
    bop->completion_cb = BlockCompleteCb;
    bop->cookie = msg;
    queue->push_back(msg);
}

}  // namespace

TransactionGroup::TransactionGroup(zx_handle_t fifo, txnid_t txnid) :
    fifo_(fifo), flags_(0), ctr_(0) {
    memset(&response_, 0, sizeof(response_));
    response_.txnid = txnid;
}

TransactionGroup::~TransactionGroup() {}

zx_status_t TransactionGroup::Enqueue(bool do_respond) {
    fbl::AutoLock lock(&lock_);
    if (flags_ & kTxnFlagRespond) {
        // Can't get more than one response for a txn
        response_.status = ZX_ERR_IO;
        goto fail;
    } else if (response_.status != ZX_OK) {
        // This operation already failed; don't bother enqueueing it.
        goto fail;
    }
    ctr_++;
    if (do_respond) {
        SetResponseReadyLocked();
    }
    return ZX_OK;
fail:
    if (do_respond) {
        SetResponseReadyLocked();
    }
    return ZX_ERR_IO;
}

void TransactionGroup::CtrAdd(uint32_t n) {
    fbl::AutoLock lock(&lock_);
    ctr_ += n;
}

void TransactionGroup::SetResponse(zx_status_t status, bool ready_to_send) {
    fbl::AutoLock lock(&lock_);

    if (response_.status == ZX_OK) {
        response_.status = status;
    }
    if (ready_to_send) {
        SetResponseReadyLocked();
    }
}

void TransactionGroup::SetResponseReadyLocked() {
    flags_ |= kTxnFlagRespond;
    if (response_.count == ctr_) {
        RespondLocked();
    }
}

void TransactionGroup::RespondLocked() {
    uint32_t actual;
    zx_status_t status = zx_fifo_write_old(fifo_, &response_,
                                           sizeof(block_fifo_response_t), &actual);
    if (status != ZX_OK) {
        fprintf(stderr, "Block Server I/O error: Could not write response\n");
    }
    response_.count = 0;
    response_.status = ZX_OK;
    ctr_ = 0;
    flags_ &= ~kTxnFlagRespond;
}

void TransactionGroup::Complete(zx_status_t status) {
    fbl::AutoLock lock(&lock_);
    if ((status != ZX_OK) && (response_.status == ZX_OK)) {
        response_.status = status;
    }

    response_.count++;
    ZX_DEBUG_ASSERT(ctr_ != 0);
    ZX_DEBUG_ASSERT(response_.count <= ctr_);

    if ((flags_ & kTxnFlagRespond) && (response_.count == ctr_)) {
        RespondLocked();
    }
}

IoBuffer::IoBuffer(zx::vmo vmo, vmoid_t id) : io_vmo_(fbl::move(vmo)), vmoid_(id) {}

IoBuffer::~IoBuffer() {}

zx_status_t IoBuffer::ValidateVmoHack(uint64_t length, uint64_t vmo_offset) {
    uint64_t vmo_size;
    zx_status_t status;
    if ((status = io_vmo_.get_size(&vmo_size)) != ZX_OK) {
        return status;
    } else if ((vmo_offset > vmo_size) || (vmo_size - vmo_offset < length)) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    return ZX_OK;
}

void BlockServer::BarrierComplete() {
    // This is the only location that unsets the OpsComplete
    // signal. We'll never "miss" a signal, because we process
    // the queue AFTER unsetting it.
    barrier_in_progress_.store(false);
    fifo_.signal(kSignalFifoOpsComplete, 0);
    InQueueDrainer();
}

void BlockServer::TerminateQueue() {
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

zx_status_t BlockServer::Read(block_fifo_request_t* requests, uint32_t* count) {
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
        status = fifo_.read_old(requests, BLOCK_FIFO_MAX_DEPTH *
                                sizeof(block_fifo_request_t), count);
        zx_signals_t signals;
        zx_signals_t seen;
        switch (status) {
        case ZX_ERR_SHOULD_WAIT:
            signals = ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED |
                    kSignalFifoTerminate | kSignalFifoOpsComplete;
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

zx_status_t BlockServer::FindVmoIDLocked(vmoid_t* out) {
    for (vmoid_t i = last_id_; i < fbl::numeric_limits<vmoid_t>::max(); i++) {
        if (!tree_.find(i).IsValid()) {
            *out = i;
            last_id_ = static_cast<vmoid_t>(i + 1);
            return ZX_OK;
        }
    }
    for (vmoid_t i = VMOID_INVALID + 1; i < last_id_; i++) {
        if (!tree_.find(i).IsValid()) {
            *out = i;
            last_id_ = static_cast<vmoid_t>(i + 1);
            return ZX_OK;
        }
    }
    return ZX_ERR_NO_RESOURCES;
}

zx_status_t BlockServer::AttachVmo(zx::vmo vmo, vmoid_t* out) {
    zx_status_t status;
    vmoid_t id;
    fbl::AutoLock server_lock(&server_lock_);
    if ((status = FindVmoIDLocked(&id)) != ZX_OK) {
        return status;
    }

    fbl::AllocChecker ac;
    fbl::RefPtr<IoBuffer> ibuf = fbl::AdoptRef(new (&ac) IoBuffer(fbl::move(vmo), id));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    tree_.insert(fbl::move(ibuf));
    *out = id;
    return ZX_OK;
}

zx_status_t BlockServer::AllocateTxn(txnid_t* out) {
    fbl::AutoLock server_lock(&server_lock_);
    for (size_t i = 0; i < fbl::count_of(txns_); i++) {
        if (txns_[i] == nullptr) {
            txnid_t txnid = static_cast<txnid_t>(i);
            fbl::AllocChecker ac;
            txns_[i] = fbl::AdoptRef(new (&ac) TransactionGroup(fifo_.get(), txnid));
            if (!ac.check()) {
                return ZX_ERR_NO_MEMORY;
            }
            *out = txnid;
            return ZX_OK;
        }
    }
    return ZX_ERR_NO_RESOURCES;
}

void BlockServer::FreeTxn(txnid_t txnid) {
    fbl::AutoLock server_lock(&server_lock_);
    if (txnid >= fbl::count_of(txns_)) {
        return;
    }
    ZX_DEBUG_ASSERT(txns_[txnid] != nullptr);
    txns_[txnid] = nullptr;
}

void BlockServer::TxnEnd() {
    size_t old_count = pending_count_.fetch_sub(1);
    ZX_ASSERT(old_count > 0);
    if ((old_count == 1) && barrier_in_progress_.load()) {
        // Since we're avoiding locking, and there is a gap between
        // "pending count decremented" and "FIFO signalled", it's possible
        // that we'll receive spurious wakeup requests.
        fifo_.signal(0, kSignalFifoOpsComplete);
    }
}

void BlockServer::InQueueDrainer() {
    while (true) {
        if (in_queue_.is_empty()) {
            return;
        }

        auto msg = in_queue_.begin();
        if (deferred_barrier_before_) {
            msg->op.command |= BLOCK_FL_BARRIER_BEFORE;
            deferred_barrier_before_ = false;
        }

        if (msg->op.command & BLOCK_FL_BARRIER_BEFORE) {
            barrier_in_progress_.store(true);
            if (pending_count_.load() > 0) {
                return;
            }
            // Since we're the only thread that could add to pending
            // count, we reliably know it has terminated.
            barrier_in_progress_.store(false);
        }
        if (msg->op.command & BLOCK_FL_BARRIER_AFTER) {
            deferred_barrier_before_ = true;
        }
        pending_count_.fetch_add(1);
        in_queue_.pop_front();
        // Underlying block device drivers should not see block barriers
        // which are already handled by the block midlayer.
        //
        // This may be altered in the future if block devices
        // are capable of implementing hardware barriers.
        msg->op.command &= ~(BLOCK_FL_BARRIER_BEFORE | BLOCK_FL_BARRIER_AFTER);
        bp_.ops->queue(bp_.ctx, &msg->op);
    }
}

zx_status_t BlockServer::Create(zx_device_t* dev, block_protocol_t* bp,
                                zx::fifo* fifo_out, BlockServer** out) {
    fbl::AllocChecker ac;
    BlockServer* bs = new (&ac) BlockServer(dev, bp);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status;
    if ((status = zx::fifo::create(BLOCK_FIFO_MAX_DEPTH, BLOCK_FIFO_ESIZE, 0,
                                   fifo_out, &bs->fifo_)) != ZX_OK) {
        delete bs;
        return status;
    }

    // Notably, drop ZX_RIGHT_SIGNAL_PEER, since we use bs->fifo for thread
    // signalling internally within the block server.
    zx_rights_t rights = ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_WRITE |
            ZX_RIGHT_SIGNAL | ZX_RIGHT_WAIT;
    if ((status = fifo_out->replace(rights, fifo_out)) != ZX_OK) {
        delete bs;
        return status;
    }

    if (bp->ops != NULL) {
        bp->ops->query(bp->ctx, &bs->info_, &bs->block_op_size_);
    }

    *out = bs;
    return ZX_OK;
}

zx_status_t BlockServer::Serve() {
    zx_status_t status;
    block_fifo_request_t requests[BLOCK_FIFO_MAX_DEPTH];
    uint32_t count;
    while (true) {
        // Attempt to drain as much of the input queue as possible
        // before (potentially) blocking in Read.
        InQueueDrainer();

        if ((status = Read(requests, &count) != ZX_OK)) {
            return status;
        }

        for (size_t i = 0; i < count; i++) {
            bool wants_reply = requests[i].opcode & BLOCKIO_TXN_END;
            txnid_t txnid = requests[i].txnid;
            vmoid_t vmoid = requests[i].vmoid;

            // TODO(ZX-1586): Remove this lock once txns are preallocated.
            fbl::AutoLock server_lock(&server_lock_);

            if (txnid >= MAX_TXN_COUNT || txns_[txnid] == nullptr) {
                // Operation which is not accessing a valid txn
                if (wants_reply) {
                    OutOfBandRespond(fifo_, ZX_ERR_IO, txnid);
                }
                continue;
            }

            auto iobuf = tree_.find(vmoid);
            if (!iobuf.IsValid()) {
                // Operation which is not accessing a valid vmo
                txns_[txnid]->SetResponse(ZX_ERR_IO, wants_reply);
                continue;
            }

            switch (requests[i].opcode & BLOCKIO_OP_MASK) {
            case BLOCKIO_READ:
            case BLOCKIO_WRITE: {
                if ((requests[i].length < 1) ||
                    (requests[i].length > fbl::numeric_limits<uint32_t>::max())) {
                    // Operation which is too small or too large
                    txns_[txnid]->SetResponse(ZX_ERR_INVALID_ARGS, wants_reply);
                    continue;
                }

                // Enqueue the message against the transaction group.
                status = txns_[txnid]->Enqueue(wants_reply);
                if (status != ZX_OK) {
                    break;
                }

                // Hack to ensure that the vmo is valid.
                // In the future, this code will be responsible for pinning VMO pages,
                // and the completion will be responsible for un-pinning those same pages.
                size_t bsz = info_.block_size;
                status = iobuf->ValidateVmoHack(bsz * requests[i].length,
                                                bsz * requests[i].vmo_offset);
                if (status != ZX_OK) {
                    txns_[txnid]->Complete(status);
                    break;
                }

                BlockMsg msg;
                if ((status = BlockMsg::Create(block_op_size_, &msg)) != ZX_OK) {
                    txns_[txnid]->Complete(status);
                    break;
                }
                msg.extra()->txn = txns_[txnid];
                msg.extra()->iobuf = iobuf.CopyPointer();
                msg.extra()->server = this;
                msg.op()->command = OpcodeToCommand(requests[i].opcode);

                const uint64_t max_xfer = info_.max_transfer_size / bsz;
                if (max_xfer != 0 && max_xfer < requests[i].length) {
                    uint64_t len_remaining = requests[i].length;
                    uint64_t vmo_offset = requests[i].vmo_offset;
                    uint64_t dev_offset = requests[i].dev_offset;

                    // If the request is larger than the maximum transfer size,
                    // split it up into a collection of smaller block messages.
                    //
                    // Once all of these smaller messages are created, splice
                    // them into the input queue together.
                    BlockMsgQueue sub_txns_queue;
                    uint32_t sub_txns =
                            static_cast<uint32_t>(fbl::round_up(len_remaining,
                                                                max_xfer) / max_xfer);
                    uint32_t sub_txn_idx = 0;
                    while (sub_txn_idx != sub_txns) {
                        // We'll be using a new BlockMsg for each sub-component.
                        if (!msg.valid()) {
                            if ((status = BlockMsg::Create(block_op_size_,
                                                           &msg)) != ZX_OK) {
                                txns_[txnid]->Complete(status);
                                break;
                            }
                            msg.extra()->txn = txns_[txnid];
                            msg.extra()->iobuf = iobuf.CopyPointer();
                            msg.extra()->server = this;
                            msg.op()->command = OpcodeToCommand(requests[i].opcode);
                        }


                        uint64_t length = fbl::min(len_remaining, max_xfer);
                        len_remaining -= length;

                        // Only set the "AFTER" barrier on the last sub-txn.
                        msg.op()->command &= ~(sub_txn_idx == sub_txns - 1 ? 0 :
                                               BLOCK_FL_BARRIER_AFTER);
                        // Only set the "BEFORE" barrier on the first sub-txn.
                        msg.op()->command &= ~(sub_txn_idx == 0 ? 0 :
                                               BLOCK_FL_BARRIER_BEFORE);
                        InQueueAdd(iobuf->vmo(), length, vmo_offset, dev_offset, msg.release(),
                                   &sub_txns_queue);
                        vmo_offset += length;
                        dev_offset += length;
                        sub_txn_idx++;
                    }
                    txns_[txnid]->CtrAdd(sub_txns - 1);
                    ZX_DEBUG_ASSERT(len_remaining == 0);

                    in_queue_.splice(in_queue_.end(), sub_txns_queue);
                } else {
                    InQueueAdd(iobuf->vmo(), requests[i].length, requests[i].vmo_offset,
                               requests[i].dev_offset, msg.release(), &in_queue_);
                }

                break;
            }
            case BLOCKIO_FLUSH: {
                fprintf(stderr, "Warning: BLOCKIO_FLUSH is currently unimplemented\n");
                break;
            }
            case BLOCKIO_CLOSE_VMO: {
                // TODO(smklein): Ensure that "iobuf" is not being used by
                // any in-flight txns.
                tree_.erase(*iobuf);
                txns_[txnid]->SetResponse(ZX_OK, wants_reply);
                break;
            }
            default: {
                fprintf(stderr, "Unrecognized Block Server operation: %x\n",
                        requests[i].opcode);
            }
            }
        }
    }
}

BlockServer::BlockServer(zx_device_t* dev, block_protocol_t* bp) :
    dev_(dev), bp_(*bp), block_op_size_(0), pending_count_(0),
    barrier_in_progress_(false), last_id_(VMOID_INVALID + 1) {
    size_t actual;
    device_ioctl(dev_, IOCTL_BLOCK_GET_INFO, nullptr, 0, &info_, sizeof(info_), &actual);
}

BlockServer::~BlockServer() {
    ZX_ASSERT(pending_count_.load() == 0);
    ZX_ASSERT(in_queue_.is_empty());
}

void BlockServer::ShutDown() {
    // Identify that the server should stop reading and return,
    // implicitly closing the fifo.
    fifo_.signal(0, kSignalFifoTerminate);
    zx_signals_t signals = kSignalFifoTerminated;
    zx_signals_t seen;
    fifo_.wait_one(signals, zx::time::infinite(), &seen);
}

// C declarations
zx_status_t blockserver_create(zx_device_t* dev, block_protocol_t* bp,
                               zx_handle_t* fifo_out, BlockServer** out) {
    zx::fifo fifo;
    zx_status_t status = BlockServer::Create(dev, bp, &fifo, out);
    *fifo_out = fifo.release();
    return status;
}
void blockserver_shutdown(BlockServer* bs) {
    bs->ShutDown();
}
void blockserver_free(BlockServer* bs) {
    delete bs;
}
zx_status_t blockserver_serve(BlockServer* bs) {
    return bs->Serve();
}
zx_status_t blockserver_attach_vmo(BlockServer* bs, zx_handle_t raw_vmo, vmoid_t* out) {
    zx::vmo vmo(raw_vmo);
    return bs->AttachVmo(fbl::move(vmo), out);
}
zx_status_t blockserver_allocate_txn(BlockServer* bs, txnid_t* out) {
    return bs->AllocateTxn(out);
}
void blockserver_free_txn(BlockServer* bs, txnid_t txnid) {
    return bs->FreeTxn(txnid);
}
