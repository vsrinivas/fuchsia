// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DEV_OPERATION_INCLUDE_LIB_OPERATION_ETHERNET_H_
#define SRC_DEVICES_LIB_DEV_OPERATION_INCLUDE_LIB_OPERATION_ETHERNET_H_

#include <fuchsia/hardware/ethernet/c/banjo.h>
#include <lib/operation/helpers/alloc_checker.h>
#include <lib/operation/operation.h>

#include <memory>

namespace eth {

// Usage notes:
//
// eth::Operation is a c++ wrapper around the ethernet_netbuf_t object. It provides
// capabilities to interact with a ethernet_netbuf buffer which is used to traverse the
// ethernet stack. On deletion, it will automatically free itself.
//
// eth::BorrowedOperation provides an unowned variant of eth::Operation. It adds
// functionality to store and call a complete callback which isn't present in
// eth::Operation.  In addition, it will call the completion on destruction if it
// wasn't already triggered.
//
// eth::OperationPool provides pooling functionality for eth::Operation reuse.
//
// eth::OperationQueue provides a queue interface for tracking eth::Operation and
// eth::BorrowedOperation objects.
//
// Available methods for both Operation and BorrowedOperation include:
//
//   ethernet_netbuf_t* operation(); // accessor for inner type.
//
//   // Takes ownership of inner type. Should only be used when transferring
//   // ownership to another driver.
//   ethernet_netbuf_t* take();
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
// eth::OperationPool<> pool;
//
// const size_t op_size = eth::Operation<>::OperationSize(parent_op_size);
// for (int i = 0; i < kNumRequest; i++) {
//     std::optional<eth::Operation> request;
//     request = eth::Operation::Alloc(op_size, parent_op_size);
//
//     if (!request) return ZX_ERR_NO_MEMORY;
//     pool.add(*std::move(request));
// }
//
///////////////////////////////////////////////////////////////////////////////
// Example: Enqueue incoming operation into a eth::OperationQueue:
//
// class Driver {
// public:
//     <...>
// private:
//     eth::BorrowedOperationQueue<> operations_;
//     const size_t parent_op_size_;
// };
//
// void Driver::EthernetImplQueueTx(ethernet_netbuf_t* op, ethernet_queue_tx_callback completion_cb,
// void* cookie) {
//     operations_.push(eth::BorrowedOperation<>(op, cb, parent_req_size_));
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
// using EthernetOperation = eth::BorrowedOperation<PrivateStorage>;
//
// void Driver::EthernetImplQueueTx(ethernet_netbuf_t* op, ethernet_queue_tx_callback completion_cb,
// void* cookie) {
//     EthernetOperation eth_op(op, cb, parent_req_size_));
//     ZX_DEBUG_ASSERT(eth_op.operation()->command == ETHERNET_IMPL_ERASE);
//     eth_op.private_storage()->valid = true;
//     eth_op.private_storage()->count_metric += 1;
//     <...>
// }
//
struct OperationTraits {
  using OperationType = ethernet_netbuf_t;

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
  using CallbackType = void(void*, zx_status_t, ethernet_netbuf_t*);

  static void Callback(CallbackType* callback, void* cookie, ethernet_netbuf_t* op,
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

template <typename Storage = void>
using OperationPool = operation::OperationPool<Operation<Storage>, OperationTraits, Storage>;

}  // namespace eth

#endif  // SRC_DEVICES_LIB_DEV_OPERATION_INCLUDE_LIB_OPERATION_ETHERNET_H_
