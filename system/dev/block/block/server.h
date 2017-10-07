// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <ddk/protocol/block.h>
#include <zircon/device/block.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

#ifdef __cplusplus

#include <zx/fifo.h>
#include <zx/vmo.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>

// Represents the mapping of "vmoid --> VMO"
class IoBuffer : public fbl::WAVLTreeContainable<fbl::RefPtr<IoBuffer>>,
                 public fbl::RefCounted<IoBuffer> {
public:
    vmoid_t GetKey() const { return vmoid_; }

    // TODO(smklein): This function is currently labelled 'hack' since we have
    // no way to ensure that the size of the VMO won't change in between
    // checking it and using it.  This will require a mechanism to "pin" VMO pages.
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

constexpr uint32_t kTxnFlagRespond = 0x00000001; // Should a reponse be sent when we hit goal?

class BlockTransaction;

typedef struct {
    fbl::RefPtr<BlockTransaction> txn;
    fbl::RefPtr<IoBuffer> iobuf;
    uint32_t opcode;
    uint32_t len_remaining;
    uint64_t vmo_offset;
    uint64_t dev_offset;
} block_msg_t;

class BlockTransaction : public fbl::RefCounted<BlockTransaction> {
public:
    BlockTransaction(zx_handle_t fifo, txnid_t txnid, block_protocol_t* proto,
                     uint32_t max_xfer);
    ~BlockTransaction();

    // Verifies that the incoming txn does not break the Block IO fifo protocol.
    // If it is successful, sets up the response_ with the registered cookie,
    // and adds to the "goal_" counter of number of Completions that must be
    // received before the transaction is identified as successful.
    zx_status_t Enqueue(bool do_respond, block_msg_t** msg_out);

    // Called once the transaction has completed successfully.
    void Complete(block_msg_t* msg, zx_status_t status);
private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(BlockTransaction);

    const zx_handle_t fifo_;
    block_protocol_t* proto_;
    const uint32_t max_xfer_;

    fbl::Mutex lock_;
    block_msg_t msgs_[MAX_TXN_MESSAGES] TA_GUARDED(lock_);
    block_fifo_response_t response_ TA_GUARDED(lock_); // The response to be sent back to the client
    uint32_t flags_ TA_GUARDED(lock_);
    uint32_t goal_ TA_GUARDED(lock_); // How many ops does the block device need to complete?
};

class BlockServer {
public:
    // Creates a new BlockServer
    static zx_status_t Create(block_protocol_t* proto, zx::fifo* fifo_out, BlockServer** out);

    // Starts the BlockServer using the current thread
    zx_status_t Serve();
    zx_status_t AttachVmo(zx::vmo vmo, vmoid_t* out);
    zx_status_t AllocateTxn(txnid_t* out);
    void FreeTxn(txnid_t txnid);

    void ShutDown();

    ~BlockServer();
private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(BlockServer);
    BlockServer(block_protocol_t* proto);

    zx_status_t Read(block_fifo_request_t* requests, uint32_t* count);
    zx_status_t FindVmoIDLocked(vmoid_t* out) TA_REQ(server_lock_);

    zx::fifo fifo_;
    block_protocol_t* proto_;
    block_info_t info_;

    fbl::Mutex server_lock_;
    fbl::WAVLTree<vmoid_t, fbl::RefPtr<IoBuffer>> tree_ TA_GUARDED(server_lock_);
    fbl::RefPtr<BlockTransaction> txns_[MAX_TXN_COUNT] TA_GUARDED(server_lock_);
    vmoid_t last_id_ TA_GUARDED(server_lock_);
};

#else

typedef struct IoBuffer IoBuffer;
typedef struct BlockServer BlockServer;

#endif  // ifdef __cplusplus

__BEGIN_CDECLS

// Allocate a new blockserver + FIFO combo
zx_status_t blockserver_create(block_protocol_t* proto, zx_handle_t* fifo_out, BlockServer** out);

// Shut down the blockserver. It will stop serving requests.
void blockserver_shutdown(BlockServer* bs);

// Free the memory allocated to the blockserver.
void blockserver_free(BlockServer* bs);

// Use the current thread to block on incoming FIFO requests.
zx_status_t blockserver_serve(BlockServer* bs);

// Attach an IO buffer to the Block Server
zx_status_t blockserver_attach_vmo(BlockServer* bs, zx_handle_t vmo, vmoid_t* out);

// Allocate & Free a txn
zx_status_t blockserver_allocate_txn(BlockServer* bs, txnid_t* out);
void blockserver_free_txn(BlockServer* bs, txnid_t txnid);

__END_CDECLS
