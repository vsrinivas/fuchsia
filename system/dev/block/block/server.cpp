// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <stdbool.h>
#include <string.h>

#include <magenta/compiler.h>
#include <magenta/device/block.h>
#include <magenta/syscalls.h>
#include <mx/fifo.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/limits.h>
#include <fbl/ref_ptr.h>

#include "server.h"

// This signal is set on the FIFO when the server should be instructed
// to terminate. Note that the block client (other end of the fifo) can
// currently also set this bit as an alternative mechanism to shut down
// the block server.
//
// If additional signals are set on the FIFO, it should be noted that
// block clients will also be able to manipulate them.
constexpr mx_signals_t kSignalFifoTerminate = MX_USER_SIGNAL_0;

static void OutOfBandErrorRespond(const mx::fifo& fifo, mx_status_t status, txnid_t txnid) {
    block_fifo_response_t response;
    response.status = status;
    response.txnid = txnid;
    response.count = 0;

    uint32_t actual;
    status = fifo.write(&response, sizeof(block_fifo_response_t), &actual);
    if (status != MX_OK) {
        fprintf(stderr, "Block Server I/O error: Could not write response\n");
    }
}

BlockTransaction::BlockTransaction(mx_handle_t fifo, txnid_t txnid) :
    fifo_(fifo), flags_(0), goal_(0) {
    memset(&response_, 0, sizeof(response_));
    response_.txnid = txnid;
}

BlockTransaction::~BlockTransaction() {}

mx_status_t BlockTransaction::Enqueue(bool do_respond, block_msg_t** msg_out) {
    fbl::AutoLock lock(&lock_);
    if (flags_ & kTxnFlagRespond) {
        // Can't get more than one response for a txn
        goto fail;
    } else if (goal_ == MAX_TXN_MESSAGES - 1) {
        // This is the last message! We expect TXN_END, and will append it
        // whether or not it was provided.
        // If it WASN'T provided, then it would not be clear when to
        // clear the current block transaction.
        do_respond = true;
    }
    MX_DEBUG_ASSERT(goal_ < MAX_TXN_MESSAGES); // Avoid overflowing msgs
    *msg_out = &msgs_[goal_++];
    flags_ |= do_respond ? kTxnFlagRespond : 0;
    return MX_OK;
fail:
    if (do_respond) {
        OutOfBandErrorRespond(mx::unowned_fifo::wrap(fifo_), MX_ERR_IO, response_.txnid);
    }
    return MX_ERR_IO;
}

void BlockTransaction::Complete(block_msg_t* msg, mx_status_t status) {
    fbl::AutoLock lock(&lock_);
    response_.count++;
    MX_DEBUG_ASSERT(goal_ != 0);
    MX_DEBUG_ASSERT(response_.count <= goal_);

    if ((status != MX_OK) && (response_.status == MX_OK)) {
        response_.status = status;
    }

    if ((flags_ & kTxnFlagRespond) && (response_.count == goal_)) {
        // Don't block the block device. Respond if we can (and in the absence
        // of an I/O error or closed remote, this should just work).
        uint32_t actual;
        mx_status_t status = mx_fifo_write(fifo_, &response_,
                                           sizeof(block_fifo_response_t), &actual);
        if (status != MX_OK) {
            fprintf(stderr, "Block Server I/O error: Could not write response\n");
        }
        response_.count = 0;
        response_.status = MX_OK;
        goal_ = 0;
        flags_ &= ~kTxnFlagRespond;
    }
    msg->txn.reset();
    msg->iobuf.reset();
}

IoBuffer::IoBuffer(mx::vmo vmo, vmoid_t id) : io_vmo_(fbl::move(vmo)), vmoid_(id) {}

IoBuffer::~IoBuffer() {}

mx_status_t IoBuffer::ValidateVmoHack(uint64_t length, uint64_t vmo_offset) {
    uint64_t vmo_size;
    mx_status_t status;
    if ((status = io_vmo_.get_size(&vmo_size)) != MX_OK) {
        return status;
    } else if (length + vmo_offset > vmo_size) {
        return MX_ERR_INVALID_ARGS;
    }
    return MX_OK;
}

mx_status_t BlockServer::Read(block_fifo_request_t* requests, uint32_t* count) {
    // Keep trying to read messages from the fifo until we have a reason to
    // terminate
    while (true) {
        mx_status_t status = fifo_.read(requests, sizeof(block_fifo_request_t), count);
        if (status == MX_ERR_SHOULD_WAIT) {
            mx_signals_t waitfor = MX_FIFO_READABLE | MX_FIFO_PEER_CLOSED | kSignalFifoTerminate;
            mx_signals_t observed;
            if ((status = fifo_.wait_one(waitfor, MX_TIME_INFINITE, &observed)) != MX_OK) {
                return status;
            }
            if ((observed & MX_FIFO_PEER_CLOSED) || (observed & kSignalFifoTerminate)) {
                return MX_ERR_PEER_CLOSED;
            }
            // Try reading again...
        } else {
            return status;
        }
    }
}

mx_status_t BlockServer::FindVmoIDLocked(vmoid_t* out) {
    for (vmoid_t i = last_id; i < fbl::numeric_limits<vmoid_t>::max(); i++) {
        if (!tree_.find(i).IsValid()) {
            *out = i;
            last_id = static_cast<vmoid_t>(i + 1);
            return MX_OK;
        }
    }
    for (vmoid_t i = 0; i < last_id; i++) {
        if (!tree_.find(i).IsValid()) {
            *out = i;
            last_id = static_cast<vmoid_t>(i + 1);
            return MX_OK;
        }
    }
    return MX_ERR_NO_RESOURCES;
}

mx_status_t BlockServer::AttachVmo(mx::vmo vmo, vmoid_t* out) {
    mx_status_t status;
    vmoid_t id;
    fbl::AutoLock server_lock(&server_lock_);
    if ((status = FindVmoIDLocked(&id)) != MX_OK) {
        return status;
    }

    fbl::AllocChecker ac;
    fbl::RefPtr<IoBuffer> ibuf = fbl::AdoptRef(new (&ac) IoBuffer(fbl::move(vmo), id));
    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }
    tree_.insert(fbl::move(ibuf));
    *out = id;
    return MX_OK;
}

mx_status_t BlockServer::AllocateTxn(txnid_t* out) {
    fbl::AutoLock server_lock(&server_lock_);
    for (size_t i = 0; i < fbl::count_of(txns_); i++) {
        if (txns_[i] == nullptr) {
            txnid_t txnid = static_cast<txnid_t>(i);
            fbl::AllocChecker ac;
            txns_[i] = fbl::AdoptRef(new (&ac) BlockTransaction(fifo_.get(), txnid));
            if (!ac.check()) {
                return MX_ERR_NO_MEMORY;
            }
            *out = txnid;
            return MX_OK;
        }
    }
    return MX_ERR_NO_RESOURCES;
}

void BlockServer::FreeTxn(txnid_t txnid) {
    fbl::AutoLock server_lock(&server_lock_);
    if (txnid >= fbl::count_of(txns_)) {
        return;
    }
    MX_DEBUG_ASSERT(txns_[txnid] != nullptr);
    txns_[txnid] = nullptr;
}

mx_status_t BlockServer::Create(mx::fifo* fifo_out, BlockServer** out) {
    fbl::AllocChecker ac;
    BlockServer* bs = new (&ac) BlockServer();
    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }

    mx_status_t status;
    if ((status = mx::fifo::create(BLOCK_FIFO_MAX_DEPTH, BLOCK_FIFO_ESIZE, 0,
                                   fifo_out, &bs->fifo_)) != MX_OK) {
        delete bs;
        return status;
    }

    *out = bs;
    return MX_OK;
}

void blockserver_fifo_complete(void* cookie, mx_status_t status) {
    block_msg_t* msg = static_cast<block_msg_t*>(cookie);
    // Since iobuf is a RefPtr, it lives at least as long as the txn,
    // and is not discarded underneath the block device driver.
    MX_DEBUG_ASSERT(msg->iobuf != nullptr);
    MX_DEBUG_ASSERT(msg->txn != nullptr);
    // Hold an extra copy of the 'txn' refptr; if we don't, and 'msg->txn' is
    // the last copy, then when we nullify 'msg->txn' in Complete we end up
    // trying to unlock a lock in a deleted BlockTxn.
    auto txn = msg->txn;
    // Pass msg to complete so 'msg->txn' can be nullified while protected
    // by the BlockTransaction's lock.
    txn->Complete(msg, status);
}

static block_callbacks_t cb = {
    blockserver_fifo_complete,
};

mx_status_t BlockServer::Serve(block_protocol_t* proto) {
    block_set_callbacks(proto, &cb);

    mx_status_t status;
    block_fifo_request_t requests[BLOCK_FIFO_MAX_DEPTH];
    uint32_t count;
    while (true) {
        if ((status = Read(requests, &count) != MX_OK)) {
            return status;
        }

        for (size_t i = 0; i < count; i++) {
            bool wants_reply = requests[i].opcode & BLOCKIO_TXN_END;
            txnid_t txnid = requests[i].txnid;
            vmoid_t vmoid = requests[i].vmoid;

            fbl::AutoLock server_lock(&server_lock_);
            auto iobuf = tree_.find(vmoid);
            if (!iobuf.IsValid()) {
                // Operation which is not accessing a valid vmo
                if (wants_reply) {
                    OutOfBandErrorRespond(fifo_, MX_ERR_IO, txnid);
                }
                continue;
            }
            if (txnid >= MAX_TXN_COUNT || txns_[txnid] == nullptr) {
                // Operation which is not accessing a valid txn
                if (wants_reply) {
                    OutOfBandErrorRespond(fifo_, MX_ERR_IO, txnid);
                }
                continue;
            }

            switch (requests[i].opcode & BLOCKIO_OP_MASK) {
            case BLOCKIO_READ:
            case BLOCKIO_WRITE: {
                block_msg_t* msg;
                status = txns_[txnid]->Enqueue(wants_reply, &msg);
                if (status != MX_OK) {
                    break;
                }
                MX_DEBUG_ASSERT(msg->txn == nullptr);
                msg->txn = txns_[txnid];
                MX_DEBUG_ASSERT(msg->iobuf == nullptr);
                msg->iobuf = iobuf.CopyPointer();

                // Hack to ensure that the vmo is valid.
                // In the future, this code will be responsible for pinning VMO pages,
                // and the completion will be responsible for un-pinning those same pages.
                status = iobuf->ValidateVmoHack(requests[i].length, requests[i].vmo_offset);
                if (status != MX_OK) {
                    cb.complete(msg, status);
                    break;
                }

                if ((requests[i].opcode & BLOCKIO_OP_MASK) == BLOCKIO_READ) {
                    block_read(proto, iobuf->io_vmo_.get(), requests[i].length,
                                     requests[i].vmo_offset, requests[i].dev_offset, msg);
                } else {
                    block_write(proto, iobuf->io_vmo_.get(), requests[i].length,
                                      requests[i].vmo_offset, requests[i].dev_offset, msg);
                }
                break;
            }
            case BLOCKIO_SYNC: {
                // TODO(smklein): It might be more useful to have this on a per-vmo basis
                fprintf(stderr, "Warning: BLOCKIO_SYNC is currently unimplemented\n");
                break;
            }
            case BLOCKIO_CLOSE_VMO: {
                tree_.erase(*iobuf);
                if (wants_reply) {
                    OutOfBandErrorRespond(fifo_, MX_OK, txnid);
                }
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

BlockServer::BlockServer() : last_id(0) {}
BlockServer::~BlockServer() {
    ShutDown();
}

void BlockServer::ShutDown() {
    // Identify that the server should stop reading and return,
    // implicitly closing the fifo.
    fifo_.signal(0, kSignalFifoTerminate);
}

// C declarations
mx_status_t blockserver_create(mx_handle_t* fifo_out, BlockServer** out) {
    mx::fifo fifo;
    mx_status_t status = BlockServer::Create(&fifo, out);
    *fifo_out = fifo.release();
    return status;
}
void blockserver_shutdown(BlockServer* bs) {
    bs->ShutDown();
}
void blockserver_free(BlockServer* bs) {
    delete bs;
}
mx_status_t blockserver_serve(BlockServer* bs, block_protocol_t* proto) {
    return bs->Serve(proto);
}
mx_status_t blockserver_attach_vmo(BlockServer* bs, mx_handle_t raw_vmo, vmoid_t* out) {
    mx::vmo vmo(raw_vmo);
    return bs->AttachVmo(fbl::move(vmo), out);
}
mx_status_t blockserver_allocate_txn(BlockServer* bs, txnid_t* out) {
    return bs->AllocateTxn(out);
}
void blockserver_free_txn(BlockServer* bs, txnid_t txnid) {
    return bs->FreeTxn(txnid);
}
