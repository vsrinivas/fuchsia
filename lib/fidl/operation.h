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

 private:
  friend class OperationBase;
  virtual void Hold(OperationBase* o) = 0;
  virtual ftl::WeakPtr<OperationContainer> Drop(OperationBase* o) = 0;
  virtual void Cont() = 0;
};

// An implementation of |OperationContainer| which runs every instance of
// Operation as soon as it arrives.
class OperationCollection : public OperationContainer {
 public:
  OperationCollection();
  ~OperationCollection() override;

 private:
  void Hold(OperationBase* o) override;
  ftl::WeakPtr<OperationContainer> Drop(OperationBase* o) override;
  void Cont() override;

  std::vector<std::unique_ptr<OperationBase>> operations_;

  FTL_DISALLOW_COPY_AND_ASSIGN(OperationCollection);
};

// An implementation of |OperationContainer| which runs incoming Operations
// sequentially. This ensures that there are no partially complete operations
// when a new operation starts.
class OperationQueue : public OperationContainer {
 public:
  OperationQueue();
  ~OperationQueue() override;

 private:
  void Hold(OperationBase* o) override;
  ftl::WeakPtr<OperationContainer> Drop(OperationBase* o) override;
  void Cont() override;

  std::queue<std::unique_ptr<OperationBase>> operations_;
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
  OperationBase(OperationContainer* container);

  // Derived classes call this when they are ready for |Run()| to be called.
  void Ready();

  // Operation::Done() calls this.
  ftl::WeakPtr<OperationContainer> DoneStart();
  void DoneFinish(ftl::WeakPtr<OperationContainer> container);

 private:
  // Used by the implementation of DoneBase() to remove this instance from the
  // container.
  OperationContainer* const container_;

  FTL_DISALLOW_COPY_AND_ASSIGN(OperationBase);
};

template<typename T>
class Operation : public OperationBase {
 public:
  ~Operation() override = default;

  using ResultCall = std::function<void(T)>;

 protected:
  Operation(OperationContainer* const container, ResultCall result_call) :
      OperationBase(container), result_call_(std::move(result_call)) {}

  // Derived classes call this when they are prepared to be removed from the
  // container. Must be the last the instance does, as it results in destructor
  // invocation.
  void Done(T v) {
    T result_value = std::move(v);
    ResultCall result_call = std::move(result_call_);
    auto container = DoneStart();
    result_call(std::move(result_value));
    DoneFinish(std::move(container));
  }

 private:
  ResultCall result_call_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Operation);
};

template<>
class Operation<void> : public OperationBase {
 public:
  ~Operation() override = default;

  using ResultCall = std::function<void()>;

 protected:
  Operation(OperationContainer* const container, ResultCall result_call) :
      OperationBase(container), result_call_(std::move(result_call)) {}

  void Done() {
    ResultCall result_call = std::move(result_call_);
    auto container = DoneStart();
    result_call();
    DoneFinish(std::move(container));
  }

 private:
  ResultCall result_call_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Operation);
};

}  // namespace modular

#endif  // APPS_MODULAR_LIB_FIDL_OPERATION_H_
