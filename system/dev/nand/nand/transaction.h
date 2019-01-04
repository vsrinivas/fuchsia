// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <stdlib.h>
#include <stdio.h>

#include <ddk/protocol/nand.h>
#include <fbl/intrusive_double_list.h>
#include <zircon/types.h>

namespace nand {

template <typename T>
struct TransactionLinkedListTraits {
    using PtrTraits = fbl::internal::ContainerPtrTraits<T>;
    static fbl::DoublyLinkedListNodeState<T>& node_state(typename PtrTraits::RefType obj) {
        return obj.data.dll_node_state_;
    }
};

struct Transaction;

// All data stored in a Transaction other than |nand_operation_t|.
class TransactionData {
private:
    TransactionData(nand_queue_callback completion_cb, void* cookie)
        : completion_cb_(completion_cb), cookie_(cookie) {}

    friend Transaction;
    friend TransactionLinkedListTraits<Transaction*>;

    nand_queue_callback completion_cb_;
    void* cookie_;
    fbl::DoublyLinkedListNodeState<Transaction*> dll_node_state_;
};

// A container for both a |nand_operation_t|, but also our arbitrary |TransactionData|.
//
// This structure is allocated by the upper level driver, and must be manually initialized
// for incoming transactions.
struct Transaction {
    // Returns a pointer to a Transaction given a nand_operation_t.
    //
    // To be used safely, the "nand_operation size" return value from |NandQuery| must
    // be at least sizeof(Transaction), requesting that enough space is allocated
    // alongside the |nand_operation_t| for the rest of |TransactionData| to fit.
    static Transaction* FromOp(nand_operation_t* op, nand_queue_callback completion_cb,
                               void* cookie) {
        static_assert(offsetof(Transaction, op) == 0, "Cannot cast from nand op to transaction");
        auto txn = reinterpret_cast<Transaction*>(op);

        // Transaction was allocated by the upper level driver, but our TransactionData
        // was not actually constructed. Use placement new to ensure the
        // object is initialized, with a complementary explicit destructor of TransactionData
        // within |Complete|.
        new (&txn->data) TransactionData(completion_cb, cookie);
        return txn;
    }

    // Since |TransactionData| is destructed in-place in calls to |Complete|, ensure
    // the typical destructor of Transaction is never executed.
    ~Transaction() = delete;

    void Complete(zx_status_t status) {
        // Since completing a transaction may de-allocate the transaction, save our state
        // and execute the placment destructor of TransactionData before invoking the
        // completion callback.
        nand_queue_callback completion_cb = data.completion_cb_;
        void* cookie = data.cookie_;

        data.~TransactionData();

        // Transaction should not be referenced after invoking the completion callback.
        completion_cb(cookie, status, &op);
    }

    nand_operation_t op;
    TransactionData data;
};

static_assert(std::is_standard_layout<Transaction>::value,
              "Transaction must be standard layout to be convertible from a nand_operation_t");

using TransactionList = fbl::DoublyLinkedList<Transaction*,
                                              TransactionLinkedListTraits<Transaction*>>;
} // namespace nand
