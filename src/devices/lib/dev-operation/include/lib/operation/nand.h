// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DEV_OPERATION_INCLUDE_LIB_OPERATION_NAND_H_
#define SRC_DEVICES_LIB_DEV_OPERATION_INCLUDE_LIB_OPERATION_NAND_H_

#include <fuchsia/hardware/nand/c/banjo.h>
#include <lib/operation/helpers/alloc_checker.h>
#include <lib/operation/operation.h>

#include <memory>

// namespace nand is used because this library will inevitably move to
// dev/lib/nand. In an effort to reduce dependencies (make doesn't support
// transitive deps), it's currently in the operation lib.
// TODO(surajmalhotra): Move to dev/lib/nand.
namespace nand {

// Usage notes:
//
// nand::Operation is a c++ wrapper around the nand_operation_t object. It provides
// capabilites to interact with a nand_op buffer which is used to traverse the
// nand stack. On deletion, it will automatically free itself.
//
// nand::BorrowedOperation provides an unowned variant of nand::Operation. It adds
// functionality to store and call a complete callback which isn't present in
// nand::Operation.  In addition, it will call the completion on destruction if it
// wasn't already triggered.
//
// nand::OperationPool provides pooling functionality for nand::Operation reuse.
//
// nand::OperationQueue provides a queue interface for tracking nand::Operation and
// nand::BorrowedOperation objects.
//
// Available methods for both Operation and BorrowedOperation include:
//
//   nand_operation_t* operation(); // accessor for inner type.
//
//   // Takes ownership of inner type. Should only be used when transferring
//   // ownership to another driver.
//   nand_operation_t* take();
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
// nand::OperationPool<> pool;
//
// const size_t op_size = nand::Operation<>::OperationSize(parent_op_size);
// for (int i = 0; i < kNumRequest; i++) {
//     std::optional<nand::Operation> request;
//     request = nand::Operation::Alloc(op_size, parent_op_size);
//
//     if (!request) return ZX_ERR_NO_MEMORY;
//     pool.add(*std::move(request));
// }
//
///////////////////////////////////////////////////////////////////////////////
// Example: Enqueue incoming operation into a nand::OperationQueue:
//
// class Driver {
// public:
//     <...>
// private:
//     nand::BorrowedOperationQueue<> operations_;
//     const size_t parent_op_size_;
// };
//
// void Driver::NandQueue(nand_operation_t* op, nand_queue_callback completion_cb, void* cookie) {
//     operations_.push(nand::BorrowedOperation<>(op, cb, parent_req_size_));
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
// using NandOperation = nand::BorrowedOperation<PrivateStorage>;
//
// void Driver::NandQueue(nand_operation_t* op, nand_queue_callback completion_cb, void* cookie) {
//     NandOperation nand_op(op, cb, parent_req_size_));
//     ZX_DEBUG_ASSERT(nand_op.operation()->command == NAND_ERASE);
//     nand_op.private_storage()->valid = true;
//     nand_op.private_storage()->count_metric += 1;
//     <...>
// }
//
struct OperationTraits {
  using OperationType = nand_operation_t;

  static OperationType* Alloc(size_t op_size) {
    operation::AllocChecker ac;
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
  using CallbackType = void(void*, zx_status_t, nand_operation_t*);

  static void Callback(CallbackType* callback, void* cookie, nand_operation_t* op,
                       zx_status_t status) {
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
using OperationQueue = operation::OperationQueue<Operation<Storage>, OperationTraits, Storage>;

template <typename Storage = void>
using BorrowedOperationQueue =
    operation::BorrowedOperationQueue<BorrowedOperation<Storage>, OperationTraits, CallbackTraits,
                                      Storage>;

}  // namespace nand

#endif  // SRC_DEVICES_LIB_DEV_OPERATION_INCLUDE_LIB_OPERATION_NAND_H_
