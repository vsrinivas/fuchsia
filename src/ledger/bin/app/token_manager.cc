// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/token_manager.h"

#include <lib/callback/scoped_callback.h>
#include <lib/fit/function.h>

#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace ledger {

TokenManager::TokenManager() : outstanding_token_count(0), weak_factory_(this) {}

TokenManager::~TokenManager() = default;

void TokenManager::set_on_empty(fit::closure on_empty_callback) {
  on_empty_callback_ = std::move(on_empty_callback);
}

ExpiringToken TokenManager::CreateToken() {
  ++outstanding_token_count;
  return ExpiringToken(callback::MakeScoped(weak_factory_.GetWeakPtr(), [this] {
    FXL_DCHECK(outstanding_token_count > 0);
    --outstanding_token_count;
    if (outstanding_token_count == 0 && on_empty_callback_) {
      on_empty_callback_();
    }
  }));
}

bool TokenManager::IsEmpty() { return outstanding_token_count == 0; }

}  // namespace ledger
