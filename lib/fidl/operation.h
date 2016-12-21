// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_FIDL_OPERATION_H_
#define APPS_MODULAR_LIB_FIDL_OPERATION_H_

#include <memory>
#include <queue>
#include <vector>

#include "lib/ftl/macros.h"

namespace modular {
class Operation;

// An abstract base class which provides methods to hold on to Operation
// instances until they declare themselves to be Done().
class OperationContainer {
 public:
  OperationContainer() = default;
  virtual ~OperationContainer() = default;

 private:
  friend class Operation;
  virtual void Hold(Operation* o) = 0;
  virtual void Drop(Operation* o) = 0;
};

// An implementation of |OperationContainer| which runs every instance of
// Operation as soon as it arrives.
class OperationCollection : public OperationContainer {
 public:
  OperationCollection() = default;

 private:
  void Hold(Operation* o) override;
  void Drop(Operation* o) override;

  std::vector<std::unique_ptr<Operation>> operations_;

  FTL_DISALLOW_COPY_AND_ASSIGN(OperationCollection);
};

// An implementation of |OperationContainer| which runs incoming Operations in a
// first-in-first-out order. All operations would be run sequentially, which
// would ensure that there are no partially complete operations when a new
// operation starts.
class OperationQueue : public OperationContainer {
 public:
  OperationQueue() = default;

 private:
  void Hold(Operation* o) override;
  void Drop(Operation* o) override;

  std::queue<std::unique_ptr<Operation>> operations_;

  FTL_DISALLOW_COPY_AND_ASSIGN(OperationQueue);
};

// Something that can be put in an OperationContainer until it calls
// Done() on itself. Used to implement asynchronous operations that
// need to hold on to handles until the operation asynchronously
// completes and returns a value.
//
// Held by a unique_ptr<> in the OperationContainer, so instances of
// derived classes need to be created with new.
//
// Advantages of using an Operation instance to implement asynchronous
// fidl method invocations:
//
//  1. It's possible in the first place. To receive the return
//     callback, the interface pointer on which the method is invoked
//     needs to be kept around. An instance allows this, but it's
//     tricky to place a move-only interface pointer on the capture
//     list of a lambda passed as argument to its own method.
//
//  2. The capture list of the callbacks only holds this, everything
//     else that needs to be passed on is in the instance.
//
//  3. Completion callbacks don't need to be made copyable and don't
//     need to be mutable, because no move only pointer is pulled from
//     their capture list.
//
//  4. Conversion of Handle to Ptr can be done by Bind() because the
//     Ptr is already there.
class Operation {
 public:
  virtual ~Operation() = default;

  // Derived classes need to implement this method which will get called when
  // this Operation gets scheduled. |Done()| must be called after |Run()|
  // completes.
  virtual void Run() = 0;

 protected:
  // Derived classes need to pass the OperationContainer here. The
  // constructor adds the instance to the container.
  Operation(OperationContainer* const container);

  // Derived classes call this when they are ready for |Run()| to be called.
  void Ready();

  // Derived classes call this when they are prepared to be removed
  // from the container. Must be the last the instance does, as it
  // results in destructor invocation.
  void Done();

 private:
  // Used by the implementation of Done() to remove this instance from
  // the container.
  OperationContainer* const container_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Operation);
};

}  // namespace modular

#endif  // APPS_MODULAR_LIB_FIDL_OPERATION_H_
