// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_STORY_MANAGER_TRANSACTION_H_
#define APPS_MODULAR_STORY_MANAGER_TRANSACTION_H_

#include <memory>
#include <vector>

#include "lib/ftl/macros.h"

namespace modular {
class Transaction;

// Holds on to Transaction instances until they declare themselves to
// be Done().
class TransactionContainer {
 public:
  TransactionContainer() = default;

 private:
  friend class Transaction;
  void Hold(Transaction* t);
  void Drop(Transaction* t);

 private:
  std::vector<std::unique_ptr<Transaction>> transactions_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TransactionContainer);
};

// Something that can be put in a TransactionContainer until it calls
// Done() on itself. Used to implement asynchronous operations that
// need to hold on to handles until the operation asynchronously
// returns a value.
//
// Held by a unique_ptr<> in the TransactionContainer, so instances of
// derived classes need to be created with new.
//
// Advantages of using a Transaction instance to implement
// asynchronous mojo method invocations:
//
//  1. It's possible in the first place. To receive the return callback,
//     the interface pointer on which the method is invoked needs to be
//     kept around. An instance allows this.
//
//  2. The capture list of the callbacks only holds this, everything
//     else that needs to be passed on is in the instance.
//
//  3. Return callbacks don't need to be made copyable, and the
//     callback lambda don't need to be mutable.
//
//  4. Conversion of Handle to Ptr can be done by Bind() because the
//     Ptr is already there.
class Transaction {
 public:
  virtual ~Transaction() = default;

 protected:
  // Derived classes need to pass the TransactionContainer here. The
  // constructor adds the instance to the container.
  Transaction(TransactionContainer* const container);

  // Derived classes call this when they are prepared to be removed
  // from the transaction container. Must be the last the instance
  // does, as it results in destructor invocation.
  void Done();

 private:
  // Used by the implementation of Done() to remove this instance from
  // the container.
  TransactionContainer* const container_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Transaction);
};

}  // namespace modular

#endif
