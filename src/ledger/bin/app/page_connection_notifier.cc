// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_connection_notifier.h"

#include <lib/callback/scoped_callback.h>
#include <lib/fit/function.h>

#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace ledger {

PageConnectionNotifier::PageConnectionNotifier(std::string ledger_name, storage::PageId page_id,
                                               std::vector<PageUsageListener*> page_usage_listeners)
    : ledger_name_(std::move(ledger_name)),
      page_id_(std::move(page_id)),
      page_usage_listeners_(std::move(page_usage_listeners)),
      weak_factory_(this) {}

PageConnectionNotifier::~PageConnectionNotifier() {}

void PageConnectionNotifier::RegisterExternalRequest() {
  if (has_external_requests_) {
    return;
  }
  has_external_requests_ = true;
  for (const auto& page_usage_listener : page_usage_listeners_) {
    page_usage_listener->OnExternallyUsed(ledger_name_, page_id_);
  }
}

void PageConnectionNotifier::UnregisterExternalRequests() {
  if (has_external_requests_) {
    auto weak_this = weak_factory_.GetWeakPtr();
    // This might delete the PageConnectionNotifier object.
    for (const auto& page_usage_listener : page_usage_listeners_) {
      page_usage_listener->OnExternallyUnused(ledger_name_, page_id_);
    }
    if (!weak_this) {
      return;
    }
    has_external_requests_ = false;
    CheckEmpty();
  }
}

ExpiringToken PageConnectionNotifier::NewInternalRequestToken() {
  if (internal_request_count_ == 0) {
    for (const auto& page_usage_listener : page_usage_listeners_) {
      page_usage_listener->OnInternallyUsed(ledger_name_, page_id_);
    }
  }
  ++internal_request_count_;
  return ExpiringToken(callback::MakeScoped(weak_factory_.GetWeakPtr(), [this] {
    FXL_DCHECK(internal_request_count_ > 0);
    --internal_request_count_;
    if (internal_request_count_ == 0) {
      auto weak_this = weak_factory_.GetWeakPtr();
      // This might delete the PageConnectionNotifier object.
      for (const auto& page_usage_listener : page_usage_listeners_) {
        page_usage_listener->OnInternallyUnused(ledger_name_, page_id_);
      }
      if (weak_this) {
        CheckEmpty();
      }
    }
  }));
}

void PageConnectionNotifier::set_on_empty(fit::closure on_empty_callback) {
  on_empty_callback_ = std::move(on_empty_callback);
}

bool PageConnectionNotifier::IsEmpty() {
  return internal_request_count_ == 0 && !has_external_requests_;
}

void PageConnectionNotifier::CheckEmpty() {
  if (IsEmpty() && on_empty_callback_) {
    on_empty_callback_();
  }
}

}  // namespace ledger
