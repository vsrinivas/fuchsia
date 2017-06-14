// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CALLBACK_PENDING_OPERATION_H_
#define APPS_LEDGER_SRC_CALLBACK_PENDING_OPERATION_H_

#include <memory>
#include <utility>
#include <vector>

#include "lib/ftl/functional/closure.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/weak_ptr.h"

namespace callback {

class PendingOperationManager {
 public:
  PendingOperationManager();
  ~PendingOperationManager();

  // Manage() takes an object |operation| as input, and returns a pair
  // containing both a pointer to the object, and a callback to delete it. Until
  // the callback is called, |operation| is owned by the
  // |PendingOperationManager| object.
  template <typename A>
  std::pair<A*, ftl::Closure> Manage(A operation) {
    auto deleter = std::make_unique<Deleter<A>>(std::move(operation));
    A* result = deleter->operation();
    ftl::Closure cleanup = ManagePendingOperation(std::move(deleter));
    return std::make_pair(result, std::move(cleanup));
  }

  size_t size() { return pending_operations_.size(); }

 private:
  class PendingOperation {
   public:
    PendingOperation() {}
    virtual ~PendingOperation() {}

   private:
    FTL_DISALLOW_COPY_AND_ASSIGN(PendingOperation);
  };

  template <typename A>
  class Deleter : public PendingOperation {
   public:
    explicit Deleter(A operation) : operation_(std::move(operation)) {}

    A* operation() { return &operation_; }

   private:
    A operation_;
  };

  ftl::Closure ManagePendingOperation(
      std::unique_ptr<PendingOperation> operation);

  std::vector<std::unique_ptr<PendingOperation>> pending_operations_;
  ftl::WeakPtrFactory<PendingOperationManager> weak_ptr_factory_;
};

}  // namespace callback

#endif  // APPS_LEDGER_SRC_CALLBACK_PENDING_OPERATION_H_
