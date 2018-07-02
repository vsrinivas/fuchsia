// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/page_eviction_manager_impl.h"

#include <lib/fit/function.h>
#include <zx/time.h>

#include "lib/fxl/strings/concatenate.h"
#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/app/ledger_repository_impl.h"
#include "peridot/bin/ledger/app/types.h"
#include "peridot/bin/ledger/coroutine/coroutine.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace ledger {

PageEvictionManagerImpl::PageEvictionManagerImpl(
    coroutine::CoroutineService* coroutine_service)
    : coroutine_service_(coroutine_service) {}

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

void PageEvictionManagerImpl::SetPageStateReader(
    PageStateReader* state_reader) {
  FXL_DCHECK(state_reader);
  FXL_DCHECK(!state_reader_);
  state_reader_ = state_reader;
}

void PageEvictionManagerImpl::TryCleanUp(fit::function<void(Status)> callback) {
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
  coroutine_service_->StartCoroutine(
      [this, by_timestamp_map = std::move(by_timestamp_map),
       callback = std::move(callback)](coroutine::CoroutineHandler* handler) {
        for (const auto& entry : by_timestamp_map) {
          const std::pair<std::string, std::string>* ledger_page_id =
              entry.second;
          bool can_evict;
          Status status = CanEvictPage(handler, ledger_page_id->first,
                                       ledger_page_id->second, &can_evict);
          if (status != Status::OK) {
            callback(status);
            return;
          }
          if (can_evict) {
            callback(EvictPage(ledger_page_id->first, ledger_page_id->second));
            return;
          }
        }
        callback(Status::OK);
      });
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

Status PageEvictionManagerImpl::CanEvictPage(
    coroutine::CoroutineHandler* handler, fxl::StringView ledger_name,
    storage::PageIdView page_id, bool* can_evict) {
  FXL_DCHECK(state_reader_);

  Status status;
  PageClosedAndSynced sync_state;
  auto sync_call_status =
      coroutine::SyncCall(handler,
                          [this, ledger_name = ledger_name.ToString(),
                           page_id = page_id.ToString()](auto callback) {
                            state_reader_->PageIsClosedAndSynced(
                                ledger_name, page_id, std::move(callback));
                          },
                          &status, &sync_state);
  if (sync_call_status == coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERNAL_ERROR;
  }
  *can_evict = (sync_state == PageClosedAndSynced::YES);

  return Status::OK;
}

}  // namespace ledger
