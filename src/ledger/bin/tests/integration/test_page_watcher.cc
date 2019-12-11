// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/tests/integration/test_page_watcher.h"

#include "src/ledger/lib/logging/logging.h"

namespace ledger {

TestPageWatcher::TestPageWatcher(fidl::InterfaceRequest<PageWatcher> request,
                                 fit::closure change_callback)
    : binding_(this, std::move(request)), change_callback_(std::move(change_callback)) {}

void TestPageWatcher::DelayCallback(bool delay_callback) { delay_callback_ = delay_callback; }

void TestPageWatcher::CallOnChangeCallback() {
  LEDGER_CHECK(on_change_callback_);
  on_change_callback_(last_snapshot_.NewRequest());
  on_change_callback_ = nullptr;
}

uint TestPageWatcher::GetChangesSeen() { return changes_seen_; }

ResultState TestPageWatcher::GetLastResultState() { return last_result_state_; }

PageSnapshotPtr* TestPageWatcher::GetLastSnapshot() { return &last_snapshot_; }

const PageChange& TestPageWatcher::GetLastPageChange() { return last_page_change_; }

void TestPageWatcher::OnChange(PageChange page_change, ResultState result_state,
                               OnChangeCallback callback) {
  changes_seen_++;
  last_result_state_ = result_state;
  last_page_change_ = std::move(page_change);
  last_snapshot_.Unbind();
  LEDGER_CHECK(!on_change_callback_);
  on_change_callback_ = std::move(callback);
  if (!delay_callback_) {
    CallOnChangeCallback();
  }
  change_callback_();
}

}  // namespace ledger
