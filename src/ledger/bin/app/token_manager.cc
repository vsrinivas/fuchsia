// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/token_manager.h"

#include <lib/fit/function.h>

#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/lib/callback/scoped_callback.h"

namespace ledger {

TokenManager::TokenManager() : outstanding_token_count_(0), weak_factory_(this) {}

TokenManager::~TokenManager() = default;

void TokenManager::SetOnDiscardable(fit::closure on_discardable) {
  on_discardable_ = std::move(on_discardable);
}

ExpiringToken TokenManager::CreateToken() {
  ++outstanding_token_count_;
  return ExpiringToken(callback::MakeScoped(weak_factory_.GetWeakPtr(), [this] {
    LEDGER_DCHECK(outstanding_token_count_ > 0);
    --outstanding_token_count_;
    if (outstanding_token_count_ == 0 && on_discardable_) {
      on_discardable_();
    }
  }));
}

bool TokenManager::IsDiscardable() const { return outstanding_token_count_ == 0; }

}  // namespace ledger
