// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/callback/cancellable_helper.h"

namespace callback {
namespace {
class DoneCancellable : public Cancellable {
 public:
  void Cancel() override {}
  bool IsDone() override { return true; }
  void SetOnDone(ftl::Closure callback) override {}
};
}  // namespace callback

CancellableImpl::CancellableImpl(ftl::Closure on_cancel)
    : on_cancel_(std::move(on_cancel)), is_done_(false) {}

void CancellableImpl::Cancel() {
  if (is_done_)
    return;
  is_done_ = true;
  on_cancel_();
}

bool CancellableImpl::IsDone() {
  return is_done_;
}

void CancellableImpl::SetOnDone(ftl::Closure callback) {
  FTL_DCHECK(!on_done_);
  on_done_ = std::move(callback);
}

ftl::RefPtr<callback::Cancellable> CreateDoneCancellable() {
  return ftl::AdoptRef(new DoneCancellable());
}

}  // namespace callback
