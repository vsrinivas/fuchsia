// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <optional>
#include <tuple>
#include <utility>

#include <ddk/debug.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

namespace operation {

// A problem exists whereby a series of drivers reuse the same object as it
// traverses the driver stack for a specific subsystem. There exists a public
// section specified by a banjo protocol, along with a private section for
// each layer in driver stack appended to the end of it like so:
//
// ---------------------
// | Public Definition |
// ---------------------
// | Driver 1 Private  |
// ---------------------
// | Driver 2 Private  |
// ---------------------
// |        ...        |
// ---------------------
// | Driver N Private  |
// ---------------------
//
// Driver N in this case would perform the allocation of the entire struct.
// Driver 1 in the example above would be the device driver which talks
// directly to hardware. The request would only be "owned" by a single
// driver at a time, but only Driver N (the one who allocated the request)
// would be allowed to free it.
//
// This library provides a generic solution to the private section problem
// which exists for types such as usb_request_t, node_operation_t,
// block_op_t, and others. Specialized wrappers for each of those types
// can be built on top this library.
//
// operation::Operation and operation::UnownedOperation provide some additional
// safety to prevent leaks and out of bounds accesses. They will ensure that the
// underlying buffer is returned to the caller, or delete it if the current
// owner was responsible for allocating the request. In addition, they help
// provide an easy way to determine the size of the request by simply specifying
// the parent device's operation size.
//
// operation::OperationPool provides a simple pool that allows reuse of
// pre-allocated operation::Operation objects.
//
// operation::{Unowned,}OperationQueue provide safe queues to place operations
// in while they are pending. These queues rely on intrusive node data built
// into the wrapper type, stored in the private storage section.
//
// In order to use make use of the Operation and OperationNode classes, a new
// type must be created which inherits from it like so:
//
// class Foo : public Operation<Foo, OperationTraits, void>;
// class Bar : public UnownedOperation<Bar, OperationTraits, CallbackTraits, void>;
//
// OperationTraits must be a type which implements the following function
// and type signatures:
//
//    // Defines public definition which is wrapped.
//    using OperationType = foo_operation_t;
//
//    // Performs an allocation for the operation.
//    static OperationType* Alloc(size_t op_size);
//
//    // Frees the allocation created by Alloc above.
//    static void Free(OperationType* op);
//
// CallbackTraits must be a type which implements the following function
// and type signatures:
//
//    // The type here can be anything. It should match the callback provided to
//    // the UnownedOperation constructor.
//    using CallbackType = void(void* ctx, ARGS, foo_operation_t*);
//
//    // In case Complete is not called by UnownedOperation owners, these are
//    // the args to trigger Complete with.
//    static std::tuple<ARGS> AutoCompleteArgs();
//
//    // Should call the callback, transforming and positioning args as necessary.
//    static void Callback(CallbackType*, void*, OperationType*, ARGS);
//
//    Where ARGS is a variadic number of types left to the implementer.
//

template <typename T, typename OperationTraits, typename CallbackTraits, typename Storage = void>
class OperationNode;

template <typename D, typename OperationTraits, typename CallbackTraits, typename Storage>
class OperationBase {
public:
    using NodeType = OperationNode<D, OperationTraits, CallbackTraits, Storage>;
    using OperationType = typename OperationTraits::OperationType;

    OperationType* take() __WARN_UNUSED_RESULT {
        auto* tmp = operation_;
        operation_ = nullptr;
        return tmp;
    }

    OperationType* operation() const {
        return operation_;
    }

    static constexpr size_t OperationSize(size_t parent_op_size) {
        return fbl::round_up(parent_op_size, kAlignment) + sizeof(NodeType);
    }

    size_t size() const {
        return node_offset_ + sizeof(NodeType);
    }

    // Returns private node stored inline
    NodeType* node() {
        auto* node = reinterpret_cast<NodeType*>(
            reinterpret_cast<uintptr_t>(operation_) + node_offset_);
        return node;
    }

    Storage* private_storage() {
        static_assert(!std::is_same<Storage, void>::value,
                      "private_storage not available on void type.");
        return node()->private_storage();
    }

protected:
    OperationBase(OperationType* operation, size_t parent_op_size)
        : operation_(operation),
          node_offset_(fbl::round_up(parent_op_size, kAlignment)) {
        ZX_DEBUG_ASSERT(operation != nullptr);
    }

    OperationBase(OperationBase&& other)
        : operation_(other.operation_), node_offset_(other.node_offset_) {
        other.operation_ = nullptr;
    }

    OperationBase& operator=(OperationBase&& other) {
        operation_ = other.operation_;
        node_offset_ = other.node_offset_;
        other.operation_ = nullptr;
        return *this;
    }

    OperationBase(const OperationBase& other) = delete;
    OperationBase& operator=(const OperationBase& other) = delete;

    OperationType* operation_;
    zx_off_t node_offset_;

private:
    static constexpr size_t kAlignment = alignof(NodeType);
};

template <typename D, typename OperationTraits, typename Storage = void>
class Operation : public OperationBase<D, OperationTraits, void, Storage> {
public:
    using BaseClass = OperationBase<D, OperationTraits, void, Storage>;
    using NodeType = OperationNode<D, OperationTraits, void, Storage>;
    using OperationType = typename OperationTraits::OperationType;

    // Creates a new operation with payload space of data_size.
    static std::optional<D> Alloc(size_t op_size,
                                  size_t parent_op_size = sizeof(OperationType)) {
        OperationType* op = OperationTraits::Alloc(op_size);
        if (op == nullptr) {
            return std::nullopt;
        }

        D out(op, parent_op_size);
        new (out.node()) NodeType(out.node_offset_);
        return std::move(out);
    }

    // Must be called with |operation| allocated via OperationTraits::Alloc.
    Operation(OperationType* operation, size_t parent_op_size)
        : BaseClass(operation, parent_op_size) {}

    Operation(Operation&& other)
        : BaseClass(other.operation_, other.node_offset_) {
        other.operation_ = nullptr;
    }

    Operation& operator=(Operation&& other) {
        BaseClass::operator=(std::move(other));
        return *this;
    }

    ~Operation() {
        Release();
    }

    void Release() {
        if (BaseClass::operation_) {
            BaseClass::node()->NodeType::~NodeType();
            OperationTraits::Free(BaseClass::take());
        }
    }
};

// Similar to operation::Operation, but it doesn't call free on destruction.
// This should be used to wrap NodeType* objects allocated in other
// drivers.
template <typename D, typename OperationTraits, typename CallbackTraits, typename Storage = void>
class UnownedOperation : public OperationBase<D, OperationTraits, CallbackTraits, Storage> {
public:
    using BaseClass = OperationBase<D, OperationTraits, CallbackTraits, Storage>;
    using NodeType = OperationNode<D, OperationTraits, CallbackTraits, Storage>;
    using OperationType = typename OperationTraits::OperationType;
    using CallbackType = typename CallbackTraits::CallbackType;

    UnownedOperation(OperationType* operation, const CallbackType* complete_cb, void* cookie,
                     size_t parent_op_size)
        : BaseClass(operation, parent_op_size) {
        new (BaseClass::node()) NodeType(BaseClass::node_offset_, complete_cb, cookie);
    }

    UnownedOperation(OperationType* operation, size_t parent_op_size)
        : BaseClass(operation, parent_op_size) {
        ZX_DEBUG_ASSERT(BaseClass::node()->node_offset() != 0);
    }

    UnownedOperation(UnownedOperation&& other)
        : BaseClass(other.operation_, other.node_offset_) {
        other.operation_ = nullptr;
    }

    UnownedOperation& operator=(UnownedOperation&& other) {
        BaseClass::operator=(std::move(other));
        return *this;
    }

    ~UnownedOperation() {
        // Auto-complete if it wasn't.
        auto complete = [this](auto... args) {
            this->Complete(std::forward<decltype(args)>(args)...);
        };
        std::apply(complete, CallbackTraits::AutoCompleteArgs());
    }

    // Must be called by the processor when the operation has completed or failed.
    // The operation and any virtual or physical memory obtained from it is no
    // longer valid after Complete is called.
    template <typename... Args>
    void Complete(Args... args) {
        if (BaseClass::operation_) {
            auto* complete_cb = BaseClass::node()->complete_cb();
            auto* cookie = BaseClass::node()->cookie();
            BaseClass::node()->NodeType::~NodeType();
            CallbackTraits::Callback(complete_cb, cookie, BaseClass::take(),
                                     std::forward<Args>(args)...);
        }
    }
};

// Node storage for operation::Operation and operation::UnownedOperation. Does not maintain
// ownership of underlying NodeType*. Must be transformed back into
// appopriate wrapper type to maintain correct ownership.
// It is strongly recommended to use operation::OperationPool and operation::OperationQueue to
// avoid ownership pitfalls.
template <typename T, typename OperationTraits, typename CallbackTraits, typename Storage>
class OperationNode : public fbl::DoublyLinkedListable<
                          OperationNode<T, OperationTraits, CallbackTraits, Storage>*> {
public:
    using OperationType = typename OperationTraits::OperationType;
    using CallbackType = typename CallbackTraits::CallbackType;

    OperationNode(zx_off_t node_offset, const CallbackType* complete_cb, void* cookie)
        : node_offset_(node_offset), complete_cb_(complete_cb), cookie_(cookie) {}

    ~OperationNode() = default;

    T operation() const {
        return T(
            reinterpret_cast<OperationType*>(reinterpret_cast<uintptr_t>(this) - node_offset_),
            node_offset_);
    }

    zx_off_t node_offset() const { return node_offset_; }

    const CallbackType* complete_cb() const {
        return complete_cb_;
    }

    void* cookie() const {
        return cookie_;
    }

    Storage* private_storage() {
        return &private_storage_;
    }

private:
    const zx_off_t node_offset_;
    const CallbackType* complete_cb_;
    void* cookie_;
    Storage private_storage_;
};

// Specialized version for when complete_cb is not required.
template <typename T, typename OperationTraits, typename Storage>
class OperationNode<T, OperationTraits, void, Storage> : public fbl::DoublyLinkedListable<
                                                             OperationNode<T, OperationTraits, void,
                                                                           Storage>*> {
public:
    using OperationType = typename OperationTraits::OperationType;

    explicit OperationNode(zx_off_t node_offset)
        : node_offset_(node_offset) {}

    ~OperationNode() = default;

    T operation() const {
        return T(
            reinterpret_cast<OperationType*>(reinterpret_cast<uintptr_t>(this) - node_offset_),
            node_offset_);
    }

    zx_off_t node_offset() const { return node_offset_; }

    Storage* private_storage() {
        return &private_storage_;
    }

private:
    const zx_off_t node_offset_;
    Storage private_storage_;
};

// Specialized version for when no additional storage is required.
template <typename T, typename OperationTraits, typename CallbackTraits>
class OperationNode<T, OperationTraits, CallbackTraits, void> : public fbl::DoublyLinkedListable<
                                                                    OperationNode<T,
                                                                                  OperationTraits,
                                                                                  CallbackTraits,
                                                                                  void>*> {
public:
    using OperationType = typename OperationTraits::OperationType;
    using CallbackType = typename CallbackTraits::CallbackType;

    OperationNode(zx_off_t node_offset, const CallbackType* complete_cb, void* cookie)
        : node_offset_(node_offset), complete_cb_(complete_cb), cookie_(cookie) {}

    ~OperationNode() = default;

    T operation() const {
        return T(
            reinterpret_cast<OperationType*>(reinterpret_cast<uintptr_t>(this) - node_offset_),
            node_offset_);
    }

    zx_off_t node_offset() const { return node_offset_; }

    const CallbackType* complete_cb() const {
        return complete_cb_;
    }

    void* cookie() const {
        return cookie_;
    }

private:
    const zx_off_t node_offset_;
    const CallbackType* complete_cb_;
    void* cookie_;
};

// Specialized version for when no additional storage is required, and
// complete_cb is not required.
template <typename T, typename OperationTraits>
class OperationNode<T, OperationTraits, void, void> : public fbl::DoublyLinkedListable<
                                                          OperationNode<T, OperationTraits, void,
                                                                        void>*> {
public:
    using OperationType = typename OperationTraits::OperationType;

    OperationNode(zx_off_t node_offset)
        : node_offset_(node_offset) {}

    ~OperationNode() = default;

    T operation() const {
        return T(
            reinterpret_cast<OperationType*>(reinterpret_cast<uintptr_t>(this) - node_offset_),
            node_offset_);
    }

    zx_off_t node_offset() const { return node_offset_; }

private:
    const zx_off_t node_offset_;
};

// Convenience queue wrapper around fbl::DoublyLinkedList<T>.
// The class is thread-safe.
template <typename OpType, typename OperationTraits, typename CallbackTraits, typename Storage>
class BaseQueue {
public:
    using NodeType = OperationNode<OpType, OperationTraits, CallbackTraits, Storage>;

    DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_node, node, NodeType* (C::*)());
    static_assert(has_node<OpType>::value,
                  "OpType must implement OperationNode<OpType, OperationTraits, CallbackTraits, "
                  "Storage>* node()");

    BaseQueue() {}

    ~BaseQueue() {
        Release();
    }

    BaseQueue(BaseQueue&& other) {
        fbl::AutoLock al(&other.lock_);
        queue_.swap(other.queue_);
    }

    BaseQueue& operator=(BaseQueue&& other) {
        fbl::AutoLock al1(&lock_);
        fbl::AutoLock al2(&other.lock_);
        queue_.clear();
        queue_.swap(other.queue_);
        return *this;
    }

    void push(OpType op) {
        fbl::AutoLock al(&lock_);
        auto* node = op.node();
        queue_.push_front(node);
        // Must prevent Complete/Release from being called in destructor.
        __UNUSED auto dummy = op.take();
    }

    void push_next(OpType op) {
        fbl::AutoLock al(&lock_);
        auto* node = op.node();
        queue_.push_back(node);
        // Must prevent Complete/Release from being called in destructor.
        __UNUSED auto dummy = op.take();
    }

    std::optional<OpType> pop() {
        fbl::AutoLock al(&lock_);
        auto* node = queue_.pop_back();
        if (node) {
            return std::move(node->operation());
        }
        return std::nullopt;
    }

    bool is_empty() {
        fbl::AutoLock al(&lock_);
        return queue_.is_empty();
    }

    // Releases all ops stored in the queue.
    void Release() {
        fbl::AutoLock al(&lock_);
        while (!queue_.is_empty()) {
            // Tranform back into operation to force correct destructor to run.
            __UNUSED auto op = queue_.pop_back()->operation();
        }
    }

protected:
    fbl::Mutex lock_;
    fbl::DoublyLinkedList<NodeType*> queue_ __TA_GUARDED(lock_);
};

template <typename D, typename OperationTraits, typename CallbackTraits, typename Storage = void>
using UnownedOperationQueue = BaseQueue<D, OperationTraits, CallbackTraits, Storage>;

template <typename D, typename OperationTraits, typename Storage = void>
using OperationQueue = BaseQueue<D, OperationTraits, void, Storage>;

// A driver may use operation::OperationPool for recycling their own operations.
template <typename OpType, typename OperationTraits, typename Storage = void>
class OperationPool : protected BaseQueue<OpType, OperationTraits, void, Storage> {
public:
    using BaseClass = BaseQueue<OpType, OperationTraits, void, Storage>;

    // Use constructor.
    using BaseClass::BaseClass;

    // Operate like a stack rather than a queue.
    void push(OpType op) {
        BaseClass::push_next(std::forward<OpType>(op));
    }
    using BaseClass::pop;

    using BaseClass::Release;
};

} // namespace operation
