// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/page_eviction_manager_impl.h"

#include <lib/fit/function.h>
#include <lib/fxl/strings/concatenate.h>
#include <zx/time.h>

#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/app/ledger_repository_impl.h"
#include "peridot/bin/ledger/app/page_usage_db.h"
#include "peridot/bin/ledger/app/types.h"
#include "peridot/bin/ledger/coroutine/coroutine.h"
#include "peridot/bin/ledger/storage/public/constants.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace ledger {

PageEvictionManagerImpl::PageEvictionManagerImpl(
    async_t* async, coroutine::CoroutineService* coroutine_service,
    ledger::DetachedPath db_path)
    : db_(async, db_path.SubPath({storage::kSerializationVersion,
                                  kPageUsageDbSerializationVersion})),
      coroutine_manager_(coroutine_service) {}

PageEvictionManagerImpl::~PageEvictionManagerImpl() {}

void PageEvictionManagerImpl::Init(fit::function<void(Status)> callback) {
  Status status = db_.Init();
  if (status != Status::OK) {
    callback(status);
    return;
  }
  // TODO(nellyv): This is a slow operation: We shouldn't wait for it to
  // terminate to call the callback. See LE-507.
  coroutine_manager_.StartCoroutine(
      std::move(callback), [this](coroutine::CoroutineHandler* handler,
                                  fit::function<void(Status)> callback) {
        callback(db_.MarkAllPagesClosed(handler));
      });
}

void PageEvictionManagerImpl::SetPageStateReader(
    PageStateReader* state_reader) {
  FXL_DCHECK(state_reader);
  FXL_DCHECK(!state_reader_);
  state_reader_ = state_reader;
}

void PageEvictionManagerImpl::TryCleanUp(fit::function<void(Status)> callback) {
  // TODO(nellyv): we should define some way to chose eviction policies.
  coroutine_manager_.StartCoroutine(
      std::move(callback), [this](coroutine::CoroutineHandler* handler,
                                  fit::function<void(Status)> callback) {
        // CanEvictPage is an expensive operation. Get the sorted list of all
        // pages first and call CanEvictPage exactly as many times as necessary.
        std::vector<PageUsageDb::PageInfo> pages;
        Status status = GetPagesByTimestamp(handler, &pages);
        if (status != Status::OK) {
          callback(status);
          return;
        }

        for (const auto& page_info : pages) {
          bool can_evict;
          Status status = CanEvictPage(handler, page_info.ledger_name,
                                       page_info.page_id, &can_evict);
          if (status != Status::OK) {
            callback(status);
            return;
          }
          if (can_evict) {
            callback(EvictPage(page_info.ledger_name, page_info.page_id));
            return;
          }
        }
        callback(Status::OK);
      });
}

void PageEvictionManagerImpl::OnPageOpened(fxl::StringView ledger_name,
                                           storage::PageIdView page_id) {
  coroutine_manager_.StartCoroutine(
      [this, ledger_name = ledger_name.ToString(),
       page_id = page_id.ToString()](coroutine::CoroutineHandler* handler) {
        Status status = db_.MarkPageOpened(handler, ledger_name, page_id);
        if (status != Status::OK) {
          FXL_LOG(ERROR) << "Failed to mark page as opened. Ledger name: "
                         << ledger_name << ". Page ID: " << page_id;
        }
      });
}

void PageEvictionManagerImpl::OnPageClosed(fxl::StringView ledger_name,
                                           storage::PageIdView page_id) {
  fit::function<void(Status)> callback =
      [ledger_name = ledger_name.ToString(),
       page_id = page_id.ToString()](Status status) {};
  coroutine_manager_.StartCoroutine(
      [this, ledger_name = ledger_name.ToString(),
       page_id = page_id.ToString()](coroutine::CoroutineHandler* handler) {
        Status status = db_.MarkPageClosed(handler, ledger_name, page_id);
        if (status != Status::OK) {
          FXL_LOG(ERROR) << "Failed to mark page as closed. Ledger name: "
                         << ledger_name << ". Page ID: " << page_id;
        }
      });
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

Status PageEvictionManagerImpl::GetPagesByTimestamp(
    coroutine::CoroutineHandler* handler,
    std::vector<PageUsageDb::PageInfo>* pages_info) {
  std::unique_ptr<storage::Iterator<const PageUsageDb::PageInfo>> pages_it;
  Status status = db_.GetPages(handler, &pages_it);
  if (status != Status::OK) {
    return status;
  }

  std::vector<PageUsageDb::PageInfo> pages;
  while (pages_it->Valid()) {
    // Sort out pages that are currently in use, i.e. those for which
    // timestamp is 0.
    if ((*pages_it)->timestamp.get() != 0) {
      pages.push_back(std::move(**pages_it));
    }
    pages_it->Next();
  }

  // Order pages by the last used timestamp.
  std::sort(pages.begin(), pages.end(),
            [](const PageUsageDb::PageInfo& info1,
               const PageUsageDb::PageInfo& info2) {
              if (info1.timestamp != info2.timestamp) {
                return info1.timestamp < info2.timestamp;
              }
              int comparison = info1.ledger_name.compare(info2.ledger_name);
              if (comparison != 0) {
                return comparison < 0;
              }
              return info1.page_id < info2.page_id;
            });

  pages_info->swap(pages);
  return Status::OK;
}

}  // namespace ledger
