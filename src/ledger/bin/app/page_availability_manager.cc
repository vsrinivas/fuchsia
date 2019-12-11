// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_availability_manager.h"

#include <lib/fit/function.h>

#include <optional>
#include <vector>

#include "src/ledger/lib/logging/logging.h"

namespace ledger {

void PageAvailabilityManager::MarkPageBusy() {
  LEDGER_DCHECK(!on_available_callbacks_) << "Page is already busy.";
  on_available_callbacks_ = {std::vector<fit::closure>{}};
}

void PageAvailabilityManager::MarkPageAvailable() {
  LEDGER_DCHECK(on_available_callbacks_) << "Page is already available.";
  for (const auto& page_callback : on_available_callbacks_.value()) {
    page_callback();
  }
  on_available_callbacks_ = std::nullopt;
  CheckDiscardable();
}

void PageAvailabilityManager::OnPageAvailable(fit::closure on_page_available) {
  if (on_available_callbacks_) {
    on_available_callbacks_.value().push_back(std::move(on_page_available));
  } else {
    on_page_available();
  }
}

void PageAvailabilityManager::SetOnDiscardable(fit::closure on_discardable) {
  on_discardable_ = std::move(on_discardable);
}

bool PageAvailabilityManager::IsDiscardable() const { return !on_available_callbacks_; }

void PageAvailabilityManager::CheckDiscardable() {
  if (IsDiscardable() && on_discardable_) {
    on_discardable_();
  }
}

}  // namespace ledger
