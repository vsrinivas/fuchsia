// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_NETWORK_WRAPPER_CANCELLABLE_H_
#define SRC_LIB_NETWORK_WRAPPER_CANCELLABLE_H_

#include <lib/fit/function.h>

#include <set>

#include "src/lib/callback/auto_cleanable.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/ref_counted.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace network_wrapper {

class AutoCancel;

// Cancellable can be used by any service that starts an asynchronous task to
// allow clients to cancel the operation. The contract is the following: When
// the client calls |Cancel|, the service should interrupt the asynchronous
// task, in particular, the service must not call any completion callbacks once
// the client called |Cancel|.
// If the client calls |Cancel|, or when the service calls any completion
// callbacks, the |IsDone| method must return |true|.
class Cancellable : public fxl::RefCountedThreadSafe<Cancellable> {
 public:
  virtual void Cancel() = 0;
  virtual bool IsDone() = 0;

 protected:
  Cancellable();
  virtual ~Cancellable();

  // The client can call the |SetOnDone| method once before the cancellable is
  // done. If the |OnDone| method has been called, the service must call the
  // |callback| after having called any completion callbacks. It must not call
  // the callback if the |Cancel| method has been called.
  virtual void SetOnDone(fit::closure callback) = 0;

 private:
  friend AutoCancel;

  FRIEND_REF_COUNTED_THREAD_SAFE(Cancellable);
};

// RAII container for a single |Cancellable|. The |Cancellable| will be canceled
// when this object is deleted.
class AutoCancel {
 public:
  explicit AutoCancel(fxl::RefPtr<Cancellable> cancellable = nullptr);
  ~AutoCancel();

  // Cancels any wrapped |Cancellable|s and starts wrapping |cancellable|.
  void Reset(fxl::RefPtr<Cancellable> cancellable = nullptr);

  // The client can call the |SetOnDiscardable| method once. |callback| will then be
  // executed when the underlying |Cancellable| finishes.
  void SetOnDiscardable(fit::closure callback);

  // Returns whether the autocancel is discardable.
  bool IsDiscardable() const;

 private:
  void OnDone();

  fxl::RefPtr<Cancellable> cancellable_;
  fit::closure on_discardable_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AutoCancel);
};

// RAII container for multiple |Cancellable|s. The |Cancellable|s will be
// canceled when this object is deleted. The |Cancellable| objects will also be
// deleted when they complete.
using CancellableContainer = callback::AutoCleanableSet<AutoCancel>;

}  // namespace network_wrapper

#endif  // SRC_LIB_NETWORK_WRAPPER_CANCELLABLE_H_
