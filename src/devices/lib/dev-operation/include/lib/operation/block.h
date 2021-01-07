// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DEV_OPERATION_INCLUDE_LIB_OPERATION_BLOCK_H_
#define SRC_DEVICES_LIB_DEV_OPERATION_INCLUDE_LIB_OPERATION_BLOCK_H_

#include <fuchsia/hardware/block/c/banjo.h>
#include <lib/operation/operation.h>

#include <memory>

namespace block {

// Usage notes:
//
// block::Operation is a c++ wrapper around the block_op_t object. It provides
// capabilites to interact with a block_op buffer which is used to traverse the
// block stack. On deletion, it will automatically free itself.
//
// block::BorrowedOperation provides an unowned variant of block::Operation. It adds
// functionality to store and call a complete callback which isn't present in
// block::Operation.  In addition, it will call the completion on destruction if it
// wasn't already triggered.
//
// block::OperationPool provides pooling functionality for block::Operation reuse.
//
// block::OperationQueue provides a queue interface for tracking block::Operation and
// block::BorrowedOperation objects.
//
// Available methods for both Operation and BorrowedOperation include:
//
//   block_op_t* operation(); // accessor for inner type.
//
//   // Takes ownership of inner type. Should only be used when transferring
//   // ownership to another driver.
//   block_op_t* take();
//
// Available to Operation and BorrowedOperation if they templatize of Storage:
//
//   Storage* private_storage(); // accessor for private storage.
//
// Available to Operation:
//
//   void Release(); // Frees the inner type.
//
// Available to BorrowedOperation:
//
//   void Complete(zx_status_t); // Completes the operation.
//
///////////////////////////////////////////////////////////////////////////////
// Example: Basic allocation with a pool:
//
// block::OperationPool<> pool;
//
// const size_t op_size = block::Operation<>::OperationSize(parent_op_size);
// for (int i = 0; i < kNumRequest; i++) {
//     std::optional<block::Operation> request;
//     request = block::Operation::Alloc(op_size, parent_op_size);
//
//     if (!request) return ZX_ERR_NO_MEMORY;
//     pool.add(*std::move(request));
// }
//
///////////////////////////////////////////////////////////////////////////////
// Example: Enqueue incoming operation into a block::OperationQueue:
//
// class Driver {
// public:
//     <...>
// private:
//     block::BorrowedOperationQueue<> operations_;
//     const size_t parent_op_size_;
// };
//
// void Driver::BlockImplQueue(block_op_t* op, block_queue_callback completion_cb, void* cookie) {
//     operations_.push(block::BorrowedOperation<>(op, cb, parent_req_size_));
// }
//
///////////////////////////////////////////////////////////////////////////////
// Example: Using private context only visible to your driver:
//
// struct PrivateStorage {
//     bool valid;
//     size_t count_metric;
// }
//
// using BlockOperation = block::BorrowedOperation<PrivateStorage>;
//
// void Driver::BlockImplQueue(block_op_t* op, block_queue_callback completion_cb, void* cookie) {
//     BlockOperation block_op(op, cb, parent_req_size_));
//     ZX_DEBUG_ASSERT(block_op.operation()->command == BLOCK_READ);
//     block_op.private_storage()->valid = true;
//     block_op.private_storage()->count_metric += 1;
//     <...>
// }
//
struct OperationTraits {
  using OperationType = block_op_t;

  static OperationType* Alloc(size_t op_size) {
    fbl::AllocChecker ac;
    std::unique_ptr<uint8_t[]> raw;
    if constexpr (alignof(OperationType) > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
      raw = std::unique_ptr<uint8_t[]>(
          new (static_cast<std::align_val_t>(alignof(OperationType)), &ac) uint8_t[op_size]);
    } else {
      raw = std::unique_ptr<uint8_t[]>(new (&ac) uint8_t[op_size]);
    }
    if (!ac.check()) {
      return nullptr;
    }
    return reinterpret_cast<OperationType*>(raw.release());
  }

  static void Free(OperationType* op) { delete[] reinterpret_cast<uint8_t*>(op); }
};

struct CallbackTraits {
  using CallbackType = void(void*, zx_status_t, block_op_t*);

  static void Callback(CallbackType* callback, void* cookie, block_op_t* op, zx_status_t status) {
    callback(cookie, status, op);
  }
};

template <typename Storage = void>
class Operation : public operation::Operation<Operation<Storage>, OperationTraits, Storage> {
 public:
  using BaseClass = operation::Operation<Operation<Storage>, OperationTraits, Storage>;
  using BaseClass::BaseClass;
};

template <typename Storage = void>
class BorrowedOperation
    : public operation::BorrowedOperation<BorrowedOperation<Storage>, OperationTraits,
                                          CallbackTraits, Storage> {
 public:
  using BaseClass = operation::BorrowedOperation<BorrowedOperation<Storage>, OperationTraits,
                                                 CallbackTraits, Storage>;
  using BaseClass::BaseClass;
};

template <typename Storage = void>
using OperationPool = operation::OperationPool<Operation<Storage>, OperationTraits, Storage>;

template <typename Storage = void>
using OperationQueue = operation::OperationQueue<Operation<Storage>, OperationTraits, Storage>;

template <typename Storage = void>
using BorrowedOperationQueue =
    operation::BorrowedOperationQueue<BorrowedOperation<Storage>, OperationTraits, CallbackTraits,
                                      Storage>;

}  // namespace block

#endif  // SRC_DEVICES_LIB_DEV_OPERATION_INCLUDE_LIB_OPERATION_BLOCK_H_
