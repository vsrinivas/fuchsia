// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/page_eviction_manager_impl.h"

#include <zx/time.h>

#include "lib/fxl/strings/concatenate.h"
#include "peridot/bin/ledger/app/constants.h"

namespace ledger {

PageEvictionManagerImpl::PageEvictionManagerImpl() {}

PageEvictionManagerImpl::~PageEvictionManagerImpl() {}

Status PageEvictionManagerImpl::Init() {
  // Update timestamps for pages potentially left open on previous run.
  zx::time now = zx::clock::get(ZX_CLOCK_MONOTONIC);

  for (auto& last_used_page : last_used_map_) {
    if (last_used_page.second.get() == 0) {
      last_used_page.second = now;
    }
  }
  return Status::OK;
}

void PageEvictionManagerImpl::TryCleanUp(std::function<void(Status)> callback) {
  if (last_used_map_.empty()) {
    callback(Status::OK);
    return;
  }

  // Order pages by the last used timestamp.
  std::map<zx::time, const std::pair<std::string, std::string>*>
      by_timestamp_map;
  for (const auto& entry : last_used_map_) {
    // Sort out pages that are currently in use, i.e. those for which timestamp
    // is 0.
    if (entry.second.get() != 0) {
      by_timestamp_map[entry.second] = &entry.first;
    }
  }

  // Find and evict the LRU page that is synced to the cloud.
  // TODO(nellyv): we should define some way to chose eviction policies.
  for (const auto& entry : by_timestamp_map) {
    bool is_synced;
    Status status =
        PageIsSynced(entry.second->first, entry.second->second, &is_synced);
    if (status != Status::OK) {
      callback(status);
      return;
    }
    if (is_synced) {
      callback(EvictPage(entry.second->first, entry.second->second));
      return;
    }
  }
  callback(Status::OK);
}

void PageEvictionManagerImpl::OnPageOpened(fxl::StringView ledger_name,
                                           storage::PageIdView page_id) {
  last_used_map_[std::make_pair(ledger_name.ToString(), page_id.ToString())] =
      zx::time(0);
}

void PageEvictionManagerImpl::OnPageClosed(fxl::StringView ledger_name,
                                           storage::PageIdView page_id) {
  last_used_map_[std::make_pair(ledger_name.ToString(), page_id.ToString())] =
      zx::clock::get(ZX_CLOCK_MONOTONIC);
}

Status PageEvictionManagerImpl::EvictPage(fxl::StringView /*ledger_name*/,
                                          storage::PageIdView /*page_id*/) {
  FXL_NOTIMPLEMENTED();
  return Status::UNKNOWN_ERROR;
}

Status PageEvictionManagerImpl::PageIsSynced(fxl::StringView /*ledger_name*/,
                                             storage::PageIdView /*page_id*/,
                                             bool* is_synced) {
  FXL_NOTIMPLEMENTED();
  return Status::UNKNOWN_ERROR;
}

}  // namespace ledger
