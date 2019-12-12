// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/network_wrapper/cancellable.h"

#include <lib/fit/function.h>

#include <utility>

namespace network_wrapper {

Cancellable::Cancellable() {}

Cancellable::~Cancellable() {}

AutoCancel::AutoCancel(fxl::RefPtr<Cancellable> cancellable)
    : cancellable_(std::move(cancellable)) {
  if (cancellable_) {
    cancellable_->SetOnDone([this] { OnDone(); });
  }
}

AutoCancel::~AutoCancel() {
  if (cancellable_)
    cancellable_->Cancel();
}

void AutoCancel::Reset(fxl::RefPtr<Cancellable> cancellable) {
  if (cancellable == cancellable_)
    return;
  if (cancellable_)
    cancellable_->Cancel();
  cancellable_ = cancellable;
  if (cancellable_)
    cancellable_->SetOnDone([this] { OnDone(); });
}

void AutoCancel::SetOnDiscardable(fit::closure callback) {
  FXL_DCHECK(!on_discardable_);
  on_discardable_ = std::move(callback);
  if (cancellable_->IsDone()) {
    OnDone();
  }
}

bool AutoCancel::IsDiscardable() const { return !cancellable_ || cancellable_->IsDone(); }

void AutoCancel::OnDone() {
  if (on_discardable_) {
    on_discardable_();
  }
}

}  // namespace network_wrapper
