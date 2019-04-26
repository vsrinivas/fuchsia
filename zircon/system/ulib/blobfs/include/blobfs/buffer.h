// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <utility>

#include <blobfs/transaction-manager.h>
#include <blobfs/write-txn.h>
#include <lib/fzl/owned-vmo-mapper.h>

namespace blobfs {

// In-memory data buffer.
// This class is thread-compatible.
class Buffer {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Buffer);

    ~Buffer();

    // Initializes the buffer VMO with |blocks| blocks of size kBlobfsBlockSize.
    static zx_status_t Create(TransactionManager* transaction_manager, const size_t blocks,
                              const char* label, std::unique_ptr<Buffer>* out);

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
    void AddTransaction(size_t start, size_t disk_start, size_t length, WriteTxn* work);

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
    Buffer(TransactionManager* transaction_manager, fzl::OwnedVmoMapper mapper)
        : transaction_manager_(transaction_manager), mapper_(std::move(mapper)), start_(0),
          length_(0), capacity_(mapper_.size() / kBlobfsBlockSize) {}

    TransactionManager* transaction_manager_;
    fzl::OwnedVmoMapper mapper_;
    vmoid_t vmoid_ = VMOID_INVALID;

    // The units of all the following are "Blobfs blocks".
    size_t start_ = 0;
    size_t length_ = 0;
    const size_t capacity_;
};

} // namespace blobfs
