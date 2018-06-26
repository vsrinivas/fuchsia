// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/callback/cancellable_helper.h"

namespace callback {
namespace {
class DoneCancellable : public Cancellable {
 public:
  void Cancel() override {}
  bool IsDone() override { return true; }
  void SetOnDone(fit::closure /*callback*/) override {}
};
}  // namespace

CancellableImpl::CancellableImpl(fit::closure on_cancel)
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

void CancellableImpl::SetOnDone(fit::closure callback) {
  FXL_DCHECK(!on_done_);
  on_done_ = std::move(callback);
}

fxl::RefPtr<callback::Cancellable> CreateDoneCancellable() {
  return fxl::AdoptRef(new DoneCancellable());
}

}  // namespace callback
