// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/callback/cancellable_helper.h"

namespace callback {
namespace {
class DoneCancellable : public Cancellable {
 public:
  void Cancel() override {}
  bool IsDone() override { return true; }
  void SetOnDone(fxl::Closure /*callback*/) override {}
};
}  // namespace

CancellableImpl::CancellableImpl(fxl::Closure on_cancel)
    : is_cancelled_(false), on_cancel_(std::move(on_cancel)), is_done_(false) {}

void CancellableImpl::Cancel() {
  is_cancelled_ = true;
  if (is_done_)
    return;
  is_done_ = true;
  on_cancel_();
}

bool CancellableImpl::IsDone() {
  return is_done_;
}

void CancellableImpl::SetOnDone(fxl::Closure callback) {
  FXL_DCHECK(!on_done_);
  on_done_ = std::move(callback);
}

fxl::RefPtr<callback::Cancellable> CreateDoneCancellable() {
  return fxl::AdoptRef(new DoneCancellable());
}

}  // namespace callback
