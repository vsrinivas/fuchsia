// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/page_manager.h"

#include <algorithm>

#include "apps/ledger/services/ledger.fidl.h"
#include "apps/ledger/src/app/branch_tracker.h"
#include "lib/ftl/logging.h"

namespace ledger {
PageManager::PageManager(std::unique_ptr<storage::PageStorage> page_storage)
    : page_storage_(std::move(page_storage)) {
  pages_.set_on_empty([this] { CheckEmpty(); });
  snapshots_.set_on_empty([this]() { CheckEmpty(); });
}

PageManager::~PageManager() {}

void PageManager::BindPage(fidl::InterfaceRequest<Page> page_request) {
  pages_.emplace(this, page_storage_.get(), std::move(page_request));
}

void PageManager::BindPageSnapshot(
    std::unique_ptr<storage::CommitContents> contents,
    fidl::InterfaceRequest<PageSnapshot> snapshot_request) {
  snapshots_.emplace(std::move(snapshot_request), page_storage_.get(),
                     std::move(contents));
}

void PageManager::CheckEmpty() {
  if (on_empty_callback_ && pages_.empty() && snapshots_.empty())
    on_empty_callback_();
}

}  // namespace ledger
