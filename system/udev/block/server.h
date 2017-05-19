// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <ddk/protocol/block.h>
#include <magenta/device/block.h>
#include <magenta/thread_annotations.h>
#include <magenta/types.h>

#ifdef __cplusplus

#include <mxtl/intrusive_wavl_tree.h>
#include <mxtl/mutex.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>

// Represents the mapping of "vmoid --> VMO"
class IoBuffer : public mxtl::WAVLTreeContainable<mxtl::RefPtr<IoBuffer>>,
                 public mxtl::RefCounted<IoBuffer> {
public:
    vmoid_t GetKey() const { return vmoid_; }

    // TODO(smklein): This function is currently labelled 'hack' since we have
    // no way to ensure that the size of the VMO won't change in between
    // checking it and using it.  This will require a mechanism to "pin" VMO pages.
    mx_status_t ValidateVmoHack(uint64_t length, uint64_t vmo_offset);

    IoBuffer(mx_handle_t vmo, vmoid_t vmoid);
    ~IoBuffer();

private:
    friend class BlockServer;
    friend struct TypeWAVLTraits;
    DISALLOW_COPY_ASSIGN_AND_MOVE(IoBuffer);

    const mx_handle_t io_vmo_;
    const vmoid_t vmoid_;
};

constexpr uint32_t kTxnFlagRespond = 0x00000001; // Should a reponse be sent when we hit goal?

class BlockTransaction;

typedef struct {
    mxtl::RefPtr<BlockTransaction> txn;
    mxtl::RefPtr<IoBuffer> iobuf;
} block_msg_t;

class BlockTransaction : public mxtl::RefCounted<BlockTransaction> {
public:
    BlockTransaction(mx_handle_t fifo, txnid_t txnid);
    ~BlockTransaction();

    // Verifies that the incoming txn does not break the Block IO fifo protocol.
    // If it is successful, sets up the response_ with the registered cookie,
    // and adds to the "goal_" counter of number of Completions that must be
    // received before the transaction is identified as successful.
    mx_status_t Enqueue(bool do_respond, block_msg_t** msg_out);

    // Called once the transaction has completed successfully.
    void Complete(block_msg_t* msg, mx_status_t status);
private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(BlockTransaction);

    const mx_handle_t fifo_;

    mxtl::Mutex lock_;
    block_msg_t msgs_[MAX_TXN_MESSAGES] TA_GUARDED(lock_);
    block_fifo_response_t response_ TA_GUARDED(lock_); // The response to be sent back to the client
    uint32_t flags_ TA_GUARDED(lock_);
    uint32_t goal_ TA_GUARDED(lock_); // How many ops does the block device need to complete?
};

class BlockServer {
public:
    // Creates a new BlockServer
    static mx_status_t Create(mx_handle_t* fifo_out, BlockServer** out);

    // Starts the BlockServer using the current thread
    mx_status_t Serve(mx_device_t* dev, block_ops_t* ops);
    mx_status_t AttachVmo(mx_handle_t vmo, vmoid_t* out);
    mx_status_t AllocateTxn(txnid_t* out);
    void FreeTxn(txnid_t txnid);

    void ShutDown();

    ~BlockServer();
private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(BlockServer);
    BlockServer();

    mx_status_t FindVmoIDLocked(vmoid_t* out) TA_REQ(server_lock_);

    mxtl::Mutex server_lock_;
    mx_handle_t fifo_ TA_GUARDED(server_lock_);
    mxtl::WAVLTree<vmoid_t, mxtl::RefPtr<IoBuffer>> tree_ TA_GUARDED(server_lock_);
    mxtl::RefPtr<BlockTransaction> txns_[MAX_TXN_COUNT] TA_GUARDED(server_lock_);
    vmoid_t last_id TA_GUARDED(server_lock_);
};

#else

typedef struct IoBuffer IoBuffer;
typedef struct BlockServer BlockServer;

#endif  // ifdef __cplusplus

__BEGIN_CDECLS

// Allocate a new blockserver + FIFO combo
mx_status_t blockserver_create(mx_handle_t* fifo_out, BlockServer** out);

// Shut down the blockserver. It will stop serving requests.
void blockserver_shutdown(BlockServer* bs);

// Free the memory allocated to the blockserver.
void blockserver_free(BlockServer* bs);

// Use the current thread to block on incoming FIFO requests.
mx_status_t blockserver_serve(BlockServer* bs, mx_device_t* dev, block_ops_t* ops);

// Attach an IO buffer to the Block Server
mx_status_t blockserver_attach_vmo(BlockServer* bs, mx_handle_t vmo, vmoid_t* out);

// Allocate & Free a txn
mx_status_t blockserver_allocate_txn(BlockServer* bs, txnid_t* out);
void blockserver_free_txn(BlockServer* bs, txnid_t txnid);

__END_CDECLS
