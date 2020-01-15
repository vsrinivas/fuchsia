// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/tests/stub_product.h"

#include "src/lib/fxl/logging.h"

namespace feedback {

using fuchsia::hwinfo::ProductInfo;

void StubProduct::GetInfo(GetInfoCallback callback) {
  FXL_CHECK(!has_been_called_) << "GetInfo() can only be called once";
  has_been_called_ = true;
  callback(std::move(info_));
}

void StubProduct::CloseConnection() {
  if (binding_) {
    binding_->Unbind();
  }
}

void StubProductNeverReturns::GetInfo(GetInfoCallback callback) {}

}  // namespace feedback
