// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>

#include <fs/block-txn.h>
#include <fs/queue.h>
#include <fs/vfs.h>

#include <lib/sync/completion.h>

#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zx/vmo.h>

#include <blobfs/blobfs.h>
#include <blobfs/format.h>

namespace blobfs {

class Blobfs;
class VnodeBlob;

struct WriteRequest {
    zx_handle_t vmo;
    size_t vmo_offset;
    size_t dev_offset;
    size_t length;
};

enum class WritebackState {
    kInit,     // Initial state of a writeback queue.
    kReady,    // Indicates the queue is ready to start running.
    kRunning,  // Indicates that the queue's async processor is currently running.
    kReadOnly, // State of a writeback queue which no longer allows writes.
};

// A transaction consisting of enqueued VMOs to be written
// out to disk at specified locations.
class WriteTxn {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(WriteTxn);

    explicit WriteTxn(Blobfs* bs) : bs_(bs), vmoid_(VMOID_INVALID), block_count_(0) {}

    virtual ~WriteTxn();

    // Identifies that |nblocks| blocks of data starting at |relative_block| within the |vmo|
    // should be written out to |absolute_block| on disk at a later point in time.
    void Enqueue(zx_handle_t vmo, uint64_t relative_block, uint64_t absolute_block,
                 uint64_t nblocks);

    fbl::Vector<WriteRequest>& Requests() { return requests_; }

    // Returns the first block at which this WriteTxn exists within its VMO buffer.
    // Requires all requests within the transaction to have been copied to a single buffer.
    size_t BlkStart() const;

    // Returns the total number of blocks in all requests within the WriteTxn. This number is
    // calculated at call time, unless the WriteTxn has already been fully buffered, at which point
    // the final |block_count_| is set. This is then returned for all subsequent calls to BlkCount.
    size_t BlkCount() const;

    bool IsBuffered() const {
        return vmoid_ != VMOID_INVALID;
    }

    // Sets the source buffer for the WriteTxn to |vmoid|.
    void SetBuffer(vmoid_t vmoid);

    // Checks if the WriteTxn vmoid_ matches |vmoid|.
    bool CheckBuffer(vmoid_t vmoid) const {
        return vmoid_ == vmoid;
    }

    // Resets the transaction's state.
    void Reset() {
        requests_.reset();
        vmoid_ = VMOID_INVALID;
    }

protected:
    // Activates the transaction.
    zx_status_t Flush();

private:
    Blobfs* bs_;
    vmoid_t vmoid_;
    fbl::Vector<WriteRequest> requests_;
    size_t block_count_;
};


// A wrapper around a WriteTxn, holding references to the underlying Vnodes
// corresponding to the txn, so their Vnodes (and VMOs) are not released
// while being written out to disk.
//
// Additionally, this class allows completions to be signalled when the transaction
// has successfully completed.
class WritebackWork : public WriteTxn,
                      public fbl::SinglyLinkedListable<fbl::unique_ptr<WritebackWork>> {
public:
    using ReadyCallback = fbl::Function<bool()>;
    using SyncCallback = fs::Vnode::SyncCallback;

    // Create a WritebackWork given a vnode (which may be null)
    // Vnode is stored for duration of txn so that it isn't destroyed during the write process
    WritebackWork(Blobfs* bs, fbl::RefPtr<VnodeBlob> vnode);
    ~WritebackWork() = default;

    // Returns the WritebackWork to the default state that it was in
    // after being created. Takes in the |reason| it is being reset.
    void Reset(zx_status_t reason);

    // Returns true if the WritebackWork is "ready" to be processed. This is always true unless a
    // "ready callback" exists, in which case that callback determines the state of readiness. Once
    // a positive response is received, the ready callback is destroyed - the WritebackWork will
    // always be ready from this point forward.
    bool IsReady();

    // Adds a callback to the WritebackWork to be called before the WritebackWork is completed,
    // to ensure that it's ready for writeback.
    //
    // Only one ready callback may be set for each WritebackWork unit.
    void SetReadyCallback(ReadyCallback callback);

    // Adds a callback to the WritebackWork, such that it will be signalled when the WritebackWork
    // is flushed to disk. If no callback is set, nothing will get signalled.
    //
    // Only one sync callback may be set for each WritebackWork unit.
    void SetSyncCallback(SyncCallback callback);

    // Tells work to remove sync flag once the txn has successfully completed.
    void SetSyncComplete();

    // Persists the enqueued work to disk,
    // and resets the WritebackWork to its initial state.
    zx_status_t Complete();

private:
    // If a sync callback exists, call it with |status|.
    void InvokeSyncCallback(zx_status_t status);

    // Delete any internal members that are no longer needed.
    void ResetInternal();

    // Optional callbacks.
    ReadyCallback ready_cb_; // Call to check whether work is ready to be processed.
    SyncCallback sync_cb_; // Call after work has been completely flushed.

    bool sync_;
    fbl::RefPtr<VnodeBlob> vn_;
};

// In-memory data buffer.
// This class is thread-compatible.
class Buffer {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Buffer);

    ~Buffer();

    // Initializes the buffer VMO with |blocks| blocks of size kBlobfsBlockSize.
    static zx_status_t Create(Blobfs* blobfs, const size_t blocks, const char* label,
                              fbl::unique_ptr<Buffer>* out);

    // Adds a transaction to |txn| which reads all data into buffer
    // starting from |disk_start| on disk.
    void Load(fs::ReadTxn* txn, size_t disk_start) {
        txn->Enqueue(vmoid_, 0, disk_start, capacity_);
    }

    // Returns true if there is space available for |blocks| blocks within the buffer.
    bool IsSpaceAvailable(size_t blocks) const;

    // Copies a write transaction to the buffer.
    // Also updates the in-memory offsets of the WriteTxn's requests so they point
    // to the correct offsets in the in-memory buffer instead of their original VMOs.
    //
    // |IsSpaceAvailable| should be called before invoking this function to
    // safely guarantee that space exists within the buffer.
    void CopyTransaction(WriteTxn* txn);

    // Adds a transaction to |work| with buffer offset |start| and length |length|,
    // starting at block |disk_start| on disk.
    void AddTransaction(size_t start, size_t disk_start, size_t length, WritebackWork* work);

    // Returns true if |txn| belongs to this buffer, and if so verifies
    // that it owns the next valid set of blocks within the buffer.
    bool VerifyTransaction(WriteTxn* txn) const;

    // Given a transaction |txn|, verifies that all requests belong to this buffer
    // and then sets the transaction's buffer accordingly (if it is not already set).
    void ValidateTransaction(WriteTxn* txn);

    // Frees the first |blocks| blocks in the buffer.
    void FreeSpace(size_t blocks);

    // Frees all space within the buffer.
    void FreeAllSpace() {
        FreeSpace(length_);
    }

    size_t start() const { return start_; }
    size_t length() const { return length_; }
    size_t capacity() const { return capacity_; }

    // Reserves the next index in the buffer.
    size_t ReserveIndex() {
        return (start_ + length_++) % capacity_;
    }

    // Returns data starting at block |index| in the buffer.
    void* MutableData(size_t index) {
        ZX_DEBUG_ASSERT(index < capacity_);
        return reinterpret_cast<char*>(mapper_.start()) + (index * kBlobfsBlockSize);
    }
private:
    Buffer(Blobfs* blobfs, fzl::OwnedVmoMapper mapper)
        : blobfs_(blobfs), mapper_(fbl::move(mapper)), start_(0), length_(0),
          capacity_(mapper_.size() / kBlobfsBlockSize) {}

    Blobfs* blobfs_;
    fzl::OwnedVmoMapper mapper_;
    vmoid_t vmoid_ = VMOID_INVALID;

    // The units of all the following are "Blobfs blocks".
    size_t start_ = 0;
    size_t length_ = 0;
    const size_t capacity_;
};

// Manages an in-memory writeback buffer (and background thread,
// which flushes this buffer out to disk).
class WritebackQueue {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(WritebackQueue);

    ~WritebackQueue();

    // Initializes the WritebackBuffer at |out|
    // with a buffer of |buffer_blocks| blocks of size kBlobfsBlockSize.
    static zx_status_t Create(Blobfs* bs, const size_t buffer_blocks,
                              fbl::unique_ptr<WritebackQueue>* out);

    // Copies all transaction data referenced from |work| into the writeback buffer.
    zx_status_t Enqueue(fbl::unique_ptr<WritebackWork> work);

    bool IsReadOnly() const __TA_REQUIRES(lock_) { return state_ == WritebackState::kReadOnly; }

    size_t GetCapacity() const { return buffer_->capacity(); }
private:
    // The waiter struct may be used as a stack-allocated queue for producers.
    // It allows them to take turns putting data into the buffer when it is
    // mostly full.
    struct Waiter : public fbl::SinglyLinkedListable<Waiter*> {};
    using ProducerQueue = fs::Queue<Waiter*>;
    using WorkQueue = fs::Queue<fbl::unique_ptr<WritebackWork>>;

    WritebackQueue(fbl::unique_ptr<Buffer> buffer) : buffer_(fbl::move(buffer)) {}

    // Blocks until |blocks| blocks of data are free for the caller.
    // Doesn't actually allocate any space.
    void EnsureSpaceLocked(size_t blocks) __TA_REQUIRES(lock_);

    // Thread which asynchronously processes transactions.
    static int WritebackThread(void* arg);

    // Signalled when the writeback buffer has space to add txns.
    cnd_t work_completed_;
    // Signalled when the writeback buffer can be consumed by the background thread.
    cnd_t work_added_;

    // Work associated with the "writeback" thread, which manages work items,
    // and flushes them to disk. This thread acts as a consumer of the
    // writeback buffer.
    thrd_t worker_;

    // Use to lock resources that may be accessed asynchronously.
    fbl::Mutex lock_;

    // Buffer which stores transactions to be written out to disk.
    fbl::unique_ptr<Buffer> buffer_;

    bool unmounting_ __TA_GUARDED(lock_) = false;

    // The WritebackQueue will start off in a kInit state, and will change to kRunning when the
    // background thread is brought up. Once it is running, if an error is detected during
    // writeback, the queue is converted to kReadOnly, and no further writes are permitted.
    WritebackState state_ __TA_GUARDED(lock_) = WritebackState::kInit;

    // Tracks all the pending Writeback Work operations which exist in the
    // writeback buffer and are ready to be sent to disk.
    WorkQueue work_queue_ __TA_GUARDED(lock_){};

    // Ensures that if multiple producers are waiting for space to write their
    // transactions into the writeback buffer, they can each write in-order.
    ProducerQueue producer_queue_ __TA_GUARDED(lock_);
};
} // namespace blobfs
