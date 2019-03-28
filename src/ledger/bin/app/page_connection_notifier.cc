// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_connection_notifier.h"

#include <lib/callback/scoped_callback.h>
#include <lib/fit/function.h>
#include <lib/fxl/memory/weak_ptr.h>

#include "src/ledger/bin/storage/public/types.h"

namespace ledger {

PageConnectionNotifier::PageConnectionNotifier(
    std::string ledger_name, storage::PageId page_id,
    PageUsageListener* page_usage_listener)
    : ledger_name_(std::move(ledger_name)),
      page_id_(std::move(page_id)),
      page_usage_listener_(page_usage_listener),
      weak_factory_(this) {}

PageConnectionNotifier::~PageConnectionNotifier() {}

void PageConnectionNotifier::RegisterExternalRequest() {
  if (has_external_requests_) {
    return;
  }
  must_notify_on_page_unused_ = true;
  has_external_requests_ = true;
  page_usage_listener_->OnPageOpened(ledger_name_, page_id_);
}

void PageConnectionNotifier::UnregisterExternalRequests() {
  if (has_external_requests_) {
    page_usage_listener_->OnPageClosed(ledger_name_, page_id_);
    has_external_requests_ = false;
    CheckEmpty();
  }
}

ExpiringToken PageConnectionNotifier::NewInternalRequestToken() {
  ++internal_request_count_;
  return ExpiringToken(callback::MakeScoped(weak_factory_.GetWeakPtr(), [this] {
    FXL_DCHECK(internal_request_count_ > 0);
    --internal_request_count_;
    CheckEmpty();
  }));
}

void PageConnectionNotifier::set_on_empty(fit::closure on_empty_callback) {
  on_empty_callback_ = std::move(on_empty_callback);
}

bool PageConnectionNotifier::IsEmpty() {
  return internal_request_count_ == 0 && !has_external_requests_;
}

void PageConnectionNotifier::CheckEmpty() {
  if (!IsEmpty()) {
    return;
  }

  if (must_notify_on_page_unused_) {
    // We need to keep the object alive while |OnPageUnused| runs.
    auto token = NewInternalRequestToken();
    must_notify_on_page_unused_ = false;
    page_usage_listener_->OnPageUnused(ledger_name_, page_id_);
    // If the page is empty at this point, destructing |token| will call
    // |CheckEmpty()| again.
    return;
  }
  if (on_empty_callback_) {
    on_empty_callback_();
  }
}

}  // namespace ledger
