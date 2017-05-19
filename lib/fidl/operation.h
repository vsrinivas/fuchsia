// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_FIDL_OPERATION_H_
#define APPS_MODULAR_LIB_FIDL_OPERATION_H_

#include <memory>
#include <queue>
#include <vector>

#include "lib/ftl/macros.h"
#include "lib/ftl/memory/weak_ptr.h"

namespace modular {
class OperationBase;

// An abstract base class which provides methods to hold on to Operation
// instances until they declare themselves to be Done().
class OperationContainer {
 public:
  OperationContainer();
  virtual ~OperationContainer();

  // Checks whether the container is empty.
  virtual bool Empty() = 0;

 private:
  friend class OperationBase;
  virtual ftl::WeakPtr<OperationContainer> GetWeakPtr() = 0;
  virtual void Hold(OperationBase* o) = 0;
  virtual void Drop(OperationBase* o) = 0;
  virtual void Cont() = 0;
};

// An implementation of |OperationContainer| which runs every instance of
// Operation as soon as it arrives.
class OperationCollection : public OperationContainer {
 public:
  OperationCollection();
  ~OperationCollection() override;

  bool Empty() override;

 private:
  ftl::WeakPtr<OperationContainer> GetWeakPtr() override;
  void Hold(OperationBase* o) override;
  void Drop(OperationBase* o) override;
  void Cont() override;

  std::vector<std::unique_ptr<OperationBase>> operations_;

  // It is essential that the weak_ptr_factory is defined after the operations_
  // container, so that it gets destroyed before operations_, and hence before
  // the Operation instances in it, so that the Done() call on their
  // OperationBase sees the container weak pointer to be null. That's also why
  // we need the virtual GetWeakPtr() in the base class, rather than to put the
  // weak ptr factory in the base class.
  ftl::WeakPtrFactory<OperationContainer> weak_ptr_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(OperationCollection);
};

// An implementation of |OperationContainer| which runs incoming Operations
// sequentially. This ensures that there are no partially complete operations
// when a new operation starts.
class OperationQueue : public OperationContainer {
 public:
  OperationQueue();
  ~OperationQueue() override;

  bool Empty() override;

 private:
  ftl::WeakPtr<OperationContainer> GetWeakPtr() override;
  void Hold(OperationBase* o) override;
  void Drop(OperationBase* o) override;
  void Cont() override;

  // Are there any operations running? An operation is considered once its
  // |Run()| has been invoked, up until result callback has finished. Note that
  // an operation could be running while it is not present in |operations_|: its
  // result callback could be executing.
  bool idle_ = true;

  std::queue<std::unique_ptr<OperationBase>> operations_;

  // It is essential that the weak_ptr_factory is defined after the operations_
  // container, so that it gets destroyed before operations_, and hence before
  // the Operation instances in it, so that the Done() call on their
  // OperationBase sees the container weak pointer to be null. That's also why
  // we need the virtual GetWeakPtr() in the base class, rather than to put the
  // weak ptr factory in the base class.
  ftl::WeakPtrFactory<OperationContainer> weak_ptr_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(OperationQueue);
};

// Something that can be put in an OperationContainer until it calls Done() on
// itself. Used to implement asynchronous operations that need to hold on to
// handles until the operation asynchronously completes and returns a value.
//
// Held by a unique_ptr<> in the OperationContainer, so instances of derived
// classes need to be created with new.
//
// Advantages of using an Operation instance to implement asynchronous fidl
// method invocations:
//
//  1. To receive the return callback, the interface pointer on which the method
//     is invoked needs to be kept around. It's tricky to place a move-only
//     interface pointer on the capture list of a lambda passed as argument to
//     its own method. An instance is much simpler.
//
//  2. The capture list of the callbacks only holds this, everything else that
//     needs to be passed on is in the instance.
//
//  3. Completion callbacks don't need to be made copyable and don't need to be
//     mutable, because no move only pointer is pulled from their capture list.
//
//  4. Conversion of Handle to Ptr can be done by Bind() because the Ptr is
//     already there.
//
// Use of Operation instances must adhere to invariants:
//
// * Deleting the operation container deletes all Operation instances in it, and
//   it must be guaranteed that no callbacks of ongoing method invocations are
//   invoked after the Operation instance they access is deleted. In order to
//   accomplish this, an Operation instance must only invoke methods on fidl
//   pointers that are either owned by the Operation instance, or that are owned
//   by the immediate owner of the operation container. This way, when the
//   operation container is deleted, the destructors of all involved fidl
//   pointers are called, close their channels, and cancel all pending method
//   callbacks. If a method is invoked on a fidl pointer that lives on beyond
//   the lifetime of the operation container, this is not guaranteed.
class OperationBase {
 public:
  virtual ~OperationBase();

  // Derived classes need to implement this method which will get called when
  // this Operation gets scheduled. |Done()| must be called after |Run()|
  // completes.
  virtual void Run() = 0;

 protected:
  // Derived classes need to pass the OperationContainer here. The constructor
  // adds the instance to the container.
  OperationBase(OperationContainer* const container);

  // Derived classes call this when they are ready for |Run()| to be called.
  void Ready();

  // Derived classes call this to remove themselves from their container.
  void DoneStart();
  static void DoneFinish(ftl::WeakPtr<OperationContainer> container);

  class FlowTokenBase;

  // Used by the implementation of DoneStart() to remove this instance from the
  // container.
  ftl::WeakPtr<OperationContainer> const container_;

  // Used by FlowTokenBase to suppress Done() calls after the Operation instance
  // was deleted.
  ftl::WeakPtrFactory<OperationBase> weak_ptr_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(OperationBase);
};

template <typename T>
class Operation : public OperationBase {
 public:
  ~Operation() override { 
    // We must invalidate our weakptrs before the result callback is destroyed
    // if it still hasn't been called; otherwise, the result callback may
    // contain FlowTokens that will attempt to call Done().
    weak_ptr_factory_.InvalidateWeakPtrs();
  }

  using ResultCall = std::function<void(T)>;

 protected:
  Operation(OperationContainer* const container, ResultCall result_call)
      : OperationBase(container),
        result_call_(std::move(result_call)) {}

  // Derived classes call this when they are prepared to be removed from the
  // container. Must be the last the instance does, as it results in destructor
  // invocation.
  void Done(T v) {
    auto container = std::move(container_);
    if (container) {
      T result_value = std::move(v);
      ResultCall result_call = std::move(result_call_);
      DoneStart();
      // Can no longer refer to |this|.
      result_call(std::move(result_value));
      OperationBase::DoneFinish(std::move(container));
    }
  }

  class FlowToken;
  class FlowTokenHolder;

 private:
  ResultCall result_call_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Operation);
};

template <>
class Operation<void> : public OperationBase {
 public:
  ~Operation() override {
    // We must invalidate our weakptrs before the result callback is destroyed
    // if it still hasn't been called; otherwise, the result callback may
    // contain FlowTokens that will attempt to call Done().
    weak_ptr_factory_.InvalidateWeakPtrs();
  }

  using ResultCall = std::function<void()>;

 protected:
  Operation(OperationContainer* const container, ResultCall result_call)
      : OperationBase(container),
        result_call_(std::move(result_call)) {}

  void Done() {
    auto container = std::move(container_);
    if (container) {
      ResultCall result_call = std::move(result_call_);
      DoneStart();
      // Can no longer refer to |this|.
      result_call();
      OperationBase::DoneFinish(std::move(container));
    }
  }

  class FlowToken;
  class FlowTokenHolder;

 private:
  ResultCall result_call_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Operation);
};

// The instance of FlowToken at which the refcount reaches zero in the
// destructor calls Done() on the Operation it holds a reference of.
//
// It is an inner class of Operation so that it has access to Done(), which is
// protected.
//
// Why this is ref counted, not moved:
//
// 1. Some flows of control branch into multiple parallel branches, and then its
//    subtle to figure out which one to move it along.
//
// 2. To move something onto a capture list is more verbose than to just copy
//    it, so it would defeat the purpose of being simpler to write than the
//    status quo.
//
// 3. A lambda with something that is moveonly on its capture list is not
//    copyable anymore, and one of the points of Operation was to only have
//    copyable continuations.
//
// NOTE: You cannot mix flow tokens and explicit Done() calls. Once an Operation
// uses a flow token, this is how Done() is called, and calling Done()
// explicitly would call it twice.
//
// The parts that are not dependent on the template parameter are factored off
// into a base class, so the template specialization for T = void (below)
// becomes less verbose.
class OperationBase::FlowTokenBase {
 public:
  FlowTokenBase(OperationBase* op);
  FlowTokenBase(const FlowTokenBase& other);
  ~FlowTokenBase();

 protected:
  int refcount() const { return *refcount_; }
  bool weak_op() const { return weak_op_.get() != nullptr; }

 private:
  int* const refcount_;  // shared between copies of FlowToken.
  ftl::WeakPtr<OperationBase> weak_op_;
};

template <typename T>
class Operation<T>::FlowToken : OperationBase::FlowTokenBase {
 public:
  FlowToken(Operation<T>* const op, T* const result)
      : FlowTokenBase(op), op_(op), result_(result) {}

  FlowToken(const FlowToken& other)
      : FlowTokenBase(other), op_(other.op_), result_(other.result_) {}

  ~FlowToken() {
    // If refcount is 1 here, it will become 0 in ~FlowTokenBase.
    if (refcount() == 1 && weak_op()) {
      op_->Done(std::move(*result_));
    }
  }

 private:
  Operation<T>* const op_;  // not owned
  T* const result_;         // not owned
};

// TODO(mesch): There surely must be a way to write this as one class. But the
// missing argument of Done() when the template parameter type is void makes
// this difficult.
class Operation<void>::FlowToken : OperationBase::FlowTokenBase {
 public:
  FlowToken(Operation<void>* const op) : FlowTokenBase(op), op_(op) {}

  FlowToken(const FlowToken& other) : FlowTokenBase(other), op_(other.op_) {}

  ~FlowToken() {
    // If refcount is 1 here, it will become 0 in ~FlowTokenBase.
    if (refcount() == 1 && weak_op()) {
      op_->Done();
    }
  }

 private:
  Operation<void>* const op_;  // not owned
};

// Sometimes the asynchronous flow of control that is represented by a FlowToken
// branches, but is actually continued on exactly one branch. For example, when
// a method is called on an external fidl service, and the callback from that
// method is also scheduled as a timeout to avoid blocking the operation queue
// in case the external service misbehaves and doesn't respond.
//
// In that situation, it would be wrong to place two copies of the flow token on
// the capture list of both callbacks, because then the flow would only be
// Done() once both callbacks are destroyed. In the case where the second
// callback is a timeout, this might be really late.
//
// Instead, a single copy of the flow token is placed in a shared container, a
// reference to which is placed on the capture list of both callbacks. The first
// callback that is invoked removes the flow token from the shared container and
// propagates it from there.
//
// That way, the flow token in the shared container also acts as call-once flag.
//
// The flow token holder is a simple wrapper of a shared ptr to a unique ptr. We
// define it because such a nested smart pointer is rather unwieldy to write
// every time.
template <typename T>
class Operation<T>::FlowTokenHolder {
 public:
  using FlowToken = Operation<T>::FlowToken;

  FlowTokenHolder(const FlowToken& flow)
      : ptr_(std::make_shared<std::unique_ptr<FlowToken>>(
            std::make_unique<FlowToken>(flow))) {}

  FlowTokenHolder(const FlowTokenHolder& other) : ptr_(other.ptr_) {}

  // Calling this method again on any copy of the same FlowTokenHolder yields a
  // nullptr. Clients can check for that to enforce call once semantics.
  //
  // The method is const because it only mutates the pointed to object. It's
  // useful to exploit this loophole because this way lambdas that have a
  // FlowTokenHolder on their capture list don't need to be mutable.
  std::unique_ptr<FlowToken> Continue() const { return std::move(*ptr_); }

 private:
  std::shared_ptr<std::unique_ptr<FlowToken>> ptr_;
};

// TODO(mesch): Compiler insists on the specialized definition, even though the
// generic definition should cover the void case too.
class Operation<void>::FlowTokenHolder {
 public:
  using FlowToken = Operation<void>::FlowToken;

  FlowTokenHolder(const FlowToken& flow)
      : ptr_(std::make_shared<std::unique_ptr<FlowToken>>(
            std::make_unique<FlowToken>(flow))) {}

  FlowTokenHolder(const FlowTokenHolder& other) : ptr_(other.ptr_) {}

  std::unique_ptr<FlowToken> Continue() const { return std::move(*ptr_); }

 private:
  std::shared_ptr<std::unique_ptr<FlowToken>> ptr_;
};

// Following is a list of commonly used operations.

// An operation which immediately calls its result callback. This is useful for
// making sure that all operations that run before this have completed.
class SyncCall : public Operation<void> {
 public:
  SyncCall(OperationContainer* const container, ResultCall result_call)
      : Operation(container, std::move(result_call)) {
    Ready();
  }

  void Run() override { Done(); }

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(SyncCall);
};

}  // namespace modular

#endif  // APPS_MODULAR_LIB_FIDL_OPERATION_H_
