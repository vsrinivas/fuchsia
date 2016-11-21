// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/callback/cancellable.h"

namespace callback {

Cancellable::Cancellable() {}

Cancellable::~Cancellable() {}

AutoCancel::AutoCancel(ftl::RefPtr<Cancellable> cancellable)
    : cancellable_(cancellable) {}

AutoCancel::~AutoCancel() {
  if (cancellable_)
    cancellable_->Cancel();
}

void AutoCancel::Reset(ftl::RefPtr<Cancellable> cancellable) {
  if (cancellable == cancellable_)
    return;
  if (cancellable_)
    cancellable_->Cancel();
  cancellable_ = cancellable;
}

CancellableContainer::CancellableContainer() {}

CancellableContainer::~CancellableContainer() {
  for (const auto& cancellable : cancellables_) {
    cancellable->Cancel();
  }
}

void CancellableContainer::Reset() {
  for (const auto& cancellable : cancellables_) {
    cancellable->Cancel();
  }
  cancellables_.clear();
}

void CancellableContainer::AddCancellable(
    ftl::RefPtr<Cancellable> cancellable) {
  if (!cancellable->IsDone()) {
    cancellables_.insert(cancellable);
    // Do not keep a ftl::RefPtr<Cancellable> in the callback, otherwise
    // cancellable will own itself and will never be deleted.
    cancellable->SetOnDone([ this, cancellable = cancellable.get() ] {
      cancellables_.erase(ftl::RefPtr<Cancellable>(cancellable));
    });
  }
}

}  // namespace callback
