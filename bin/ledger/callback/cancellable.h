// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CALLBACK_CANCELLABLE_H_
#define APPS_LEDGER_SRC_CALLBACK_CANCELLABLE_H_

#include <set>

#include "apps/ledger/src/callback/auto_cleanable.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/memory/ref_ptr.h"

namespace callback {

class AutoCancel;

// Cancellable can be used by any service that starts an asynchronous task to
// allow clients to cancel the operation. The contract is the following: When
// the client calls |Cancel|, the service should interrupt the asynchronous
// task, in particular, the service must not call any completion callbacks once
// the client called |Cancel|.
// Once the client calls |Cancel|, or when the service calls any completion
// callbacks, the |IsDone| method must return |true|.
class Cancellable : public ftl::RefCountedThreadSafe<Cancellable> {
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
  virtual void SetOnDone(ftl::Closure callback) = 0;

 private:
  friend AutoCancel;

  FRIEND_REF_COUNTED_THREAD_SAFE(Cancellable);
};

// RAII container for a single |Cancellable|. The |Cancellable| will be canceled
// when this object is deleted.
class AutoCancel {
 public:
  AutoCancel(ftl::RefPtr<Cancellable> cancellable = nullptr);
  ~AutoCancel();

  // Cancel any wrapped |Cancellable| and starts wrapping |cancellable|.
  void Reset(ftl::RefPtr<Cancellable> cancellable = nullptr);

  // The client can call the |set_on_empty| method once. |callback| will then be
  // executed when the underlying Cancellable finishes.
  void set_on_empty(ftl::Closure callback);

 private:
  void OnDone();

  ftl::RefPtr<Cancellable> cancellable_;
  ftl::Closure on_empty_;

  FTL_DISALLOW_COPY_AND_ASSIGN(AutoCancel);
};

// RAII container for multiple |Cancellable|. The |Cancellable|s will be
// canceled when this object is deleted. The |Cancellable| objects will also be
// deleted when they complete.
using CancellableContainer = AutoCleanableSet<AutoCancel>;

}  // namespace callback

#endif  // APPS_LEDGER_SRC_CALLBACK_CANCELLABLE_H_
