// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_availability_manager.h"

#include <lib/fit/function.h>

#include "peridot/lib/convert/convert.h"

namespace ledger {

void PageAvailabilityManager::MarkPageBusy(
    convert::ExtendedStringView page_id) {
  auto result =
      busy_pages_.emplace(page_id.ToString(), std::vector<fit::closure>());
  FXL_DCHECK(result.second)
      << "Page " << convert::ToHex(page_id) << " is already busy.";
}

void PageAvailabilityManager::MarkPageAvailable(
    convert::ExtendedStringView page_id) {
  auto it = busy_pages_.find(page_id.ToString());
  if (it == busy_pages_.end()) {
    return;
  }

  for (auto& page_callback : it->second) {
    page_callback();
  }
  busy_pages_.erase(it);
  CheckEmpty();
}

void PageAvailabilityManager::OnPageAvailable(
    convert::ExtendedStringView page_id, fit::closure on_page_available) {
  auto it = busy_pages_.find(page_id.ToString());
  if (it == busy_pages_.end()) {
    on_page_available();
    return;
  }
  it->second.push_back(std::move(on_page_available));
}

void PageAvailabilityManager::set_on_empty(fit::closure on_empty_callback) {
  on_empty_callback_ = std::move(on_empty_callback);
}

bool PageAvailabilityManager::IsEmpty() { return busy_pages_.empty(); }

void PageAvailabilityManager::CheckEmpty() {
  if (IsEmpty() && on_empty_callback_) {
    on_empty_callback_();
  }
}

}  // namespace ledger
