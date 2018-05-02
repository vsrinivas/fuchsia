// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <zircon/device/block.h>
#include <ddk/protocol/block.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

#ifdef __cplusplus

#include <fbl/atomic.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <lib/fzl/fifo.h>
#include <lib/zx/vmo.h>
#include <sync/completion.h>

#include "txn-group.h"

// Represents the mapping of "vmoid --> VMO"
class IoBuffer : public fbl::WAVLTreeContainable<fbl::RefPtr<IoBuffer>>,
                 public fbl::RefCounted<IoBuffer> {
public:
    vmoid_t GetKey() const { return vmoid_; }

    // TODO(smklein): This function is currently labelled 'hack' since we have
    // no way to ensure that the size of the VMO won't change in between
    // checking it and using it.  This will require a mechanism to "pin" VMO pages.
    // The units of length and vmo_offset is bytes.
    zx_status_t ValidateVmoHack(uint64_t length, uint64_t vmo_offset);

    zx_handle_t vmo() const { return io_vmo_.get(); }

    IoBuffer(zx::vmo vmo, vmoid_t vmoid);
    ~IoBuffer();

private:
    friend struct TypeWAVLTraits;
    DISALLOW_COPY_ASSIGN_AND_MOVE(IoBuffer);

    const zx::vmo io_vmo_;
    const vmoid_t vmoid_;
};

class BlockServer;

typedef struct block_msg_extra block_msg_extra_t;
typedef struct block_msg block_msg_t;

// All the C++ bits of a block message. This allows the block server to utilize
// C++ libraries while also using "block_op_t"s, which may require extra space.
struct block_msg_extra {
    fbl::DoublyLinkedListNodeState<block_msg_t*> dll_node_state;
    fbl::RefPtr<IoBuffer> iobuf;
    BlockServer* server;
    reqid_t reqid;
    groupid_t group;
};

// A single unit of work transmitted to the underlying block layer.
struct block_msg {
    block_msg_extra_t extra;
    block_op_t op;
    // + Extra space for underlying block_op
};

// Since the linked list state (necessary to queue up block messages) is based
// in C++ code, but may need to reference the "block_op_t" object, it uses
// a custom type trait.
struct DoublyLinkedListTraits {
    static fbl::DoublyLinkedListNodeState<block_msg_t*>& node_state(block_msg_t& obj) {
        return obj.extra.dll_node_state;
    }
};

using BlockMsgQueue = fbl::DoublyLinkedList<block_msg_t*, DoublyLinkedListTraits>;

// C++ safe wrapper around block_msg_t.
//
// It's difficult to allocate a dynamic-length "block_op" as requested by the
// underlying driver while maintaining valid object construction & destruction;
// this class attempts to hide those details.
class BlockMsg {
public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(BlockMsg);

    bool valid() { return bop_ != nullptr; }

    void reset(block_msg_t* bop = nullptr) {
        if (bop_) {
            bop_->extra.~block_msg_extra_t();
            free(bop_);
        }
        bop_ = bop;
    }

    block_msg_t* release() {
        auto bop = bop_;
        bop_ = nullptr;
        return bop;
    }
    block_msg_extra_t* extra() { return &bop_->extra; }
    block_op_t* op() { return &bop_->op; }

    BlockMsg(block_msg_t* bop) : bop_(bop) {}
    BlockMsg() : bop_(nullptr) {}
    BlockMsg& operator=(BlockMsg&& o) {
        reset(o.release());
        return *this;
    }

    ~BlockMsg() {
        reset();
    }

    static zx_status_t Create(size_t block_op_size, BlockMsg* out) {
        block_msg_t* bop = (block_msg_t*) calloc(block_op_size + sizeof(block_msg_t) -
                                                 sizeof(block_op_t), 1);
        if (bop == nullptr) {
            return ZX_ERR_NO_MEMORY;
        }
        // Placement constructor, followed by explicit destructor in ~BlockMsg();
        new (&bop->extra) block_msg_extra_t();
        BlockMsg msg(bop);
        *out = fbl::move(msg);
        return ZX_OK;
    }

private:
    block_msg_t* bop_;
};

class BlockServer {
public:
    // Creates a new BlockServer.
    static zx_status_t Create(zx_device_t* dev, block_protocol_t* bp,
                              fzl::fifo<block_fifo_request_t, block_fifo_response_t>* fifo_out,
                              BlockServer** out);

    // Starts the BlockServer using the current thread
    zx_status_t Serve() TA_EXCL(server_lock_);
    zx_status_t AttachVmo(zx::vmo vmo, vmoid_t* out) TA_EXCL(server_lock_);

    // Updates the total number of pending txns, possibly signals
    // the queue-draining thread to wake up if they are waiting
    // for all pending operations to complete.
    //
    // Should only be called for transactions which have been placed
    // on (and removed from) in_queue_.
    void TxnEnd();

    // Wrapper around "Completed Transaction", as a convenience
    // both both one-shot and group-based transactions.
    //
    // (If appropriate) tells the client that their operation is done.
    void TxnComplete(zx_status_t status, reqid_t reqid, groupid_t group);

    void ShutDown();
    ~BlockServer();
private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(BlockServer);
    BlockServer(zx_device_t* dev, block_protocol_t* bp);

    // Helper for processing a single message read from the FIFO.
    void ProcessRequest(block_fifo_request_t* request);

    // Helper for the server to react to a signal that a barrier
    // operation has completed. Unsets the local "waiting for barrier"
    // signal, and enqueues any further operations that might be
    // pending.
    void BarrierComplete();

    // Functions that read from the fifo and invoke the queue drainer.
    // Should not be invoked concurrently.
    zx_status_t Read(block_fifo_request_t* requests, size_t* count);
    void TerminateQueue();

    // Attempts to enqueue all operations on the |in_queue_|. Stops
    // when either the queue is empty, or a BARRIER_BEFORE is reached and
    // operations are in-flight.
    void InQueueDrainer();

    zx_status_t FindVmoIDLocked(vmoid_t* out) TA_REQ(server_lock_);

    fzl::fifo<block_fifo_response_t, block_fifo_request_t> fifo_;
    zx_device_t* dev_;
    block_info_t info_;
    block_protocol_t bp_;
    size_t block_op_size_;

    // BARRIER_AFTER is implemented by sticking "BARRIER_BEFORE" on the
    // next operation that arrives.
    bool deferred_barrier_before_ = false;
    BlockMsgQueue in_queue_;
    fbl::atomic<size_t> pending_count_;
    fbl::atomic<bool> barrier_in_progress_;
    TransactionGroup groups_[MAX_TXN_GROUP_COUNT];

    fbl::Mutex server_lock_;
    fbl::WAVLTree<vmoid_t, fbl::RefPtr<IoBuffer>> tree_ TA_GUARDED(server_lock_);
    vmoid_t last_id_ TA_GUARDED(server_lock_);
};

#else

typedef struct IoBuffer IoBuffer;
typedef struct BlockServer BlockServer;

#endif  // ifdef __cplusplus

__BEGIN_CDECLS

// Allocate a new blockserver + FIFO combo
zx_status_t blockserver_create(zx_device_t* dev, block_protocol_t* bp,
                               zx_handle_t* fifo_out, BlockServer** out);

// Shut down the blockserver. It will stop serving requests.
void blockserver_shutdown(BlockServer* bs);

// Free the memory allocated to the blockserver.
void blockserver_free(BlockServer* bs);

// Use the current thread to block on incoming FIFO requests.
zx_status_t blockserver_serve(BlockServer* bs);

// Attach an IO buffer to the Block Server
zx_status_t blockserver_attach_vmo(BlockServer* bs, zx_handle_t vmo, vmoid_t* out);

__END_CDECLS
