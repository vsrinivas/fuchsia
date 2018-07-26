// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/page_eviction_manager_impl.h"

#include <lib/async/cpp/task.h>
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
namespace {

// Logs an error message if the given |status| is not |OK| or |INTERNAL_ERROR|.
void LogOnPageUpdateError(fxl::StringView operation_description, Status status,
                          fxl::StringView ledger_name,
                          storage::PageIdView page_id) {
  // Don't print an error on |INTERNAL_ERROR|: it means that the operation was
  // interrupted, because PageEvictionManagerImpl was destroyed before being
  // empty.
  if (status != Status::OK && status != Status::INTERNAL_ERROR) {
    FXL_LOG(ERROR) << "Failed to " << operation_description
                   << " in PageUsage DB. Status: " << fidl::ToUnderlying(status)
                   << ". Ledger name: " << ledger_name
                   << ". Page ID: " << convert::ToHex(page_id);
  }
}

// If the given |status| is not |OK|, logs an error message on failure to
// initialize. Returns true in case of error; false otherwise.
bool LogOnInitializationError(fxl::StringView operation_description,
                              Status status) {
  if (status != Status::OK) {
    FXL_LOG(ERROR) << operation_description
                   << " failed because of initialization error: "
                   << fidl::ToUnderlying(status);
    return true;
  }
  return false;
}

}  // namespace

PageEvictionManagerImpl::Completer::Completer() {}

PageEvictionManagerImpl::Completer::~Completer() {
  CallCallbacks(Status::INTERNAL_ERROR);
}

void PageEvictionManagerImpl::Completer::Complete(Status status) {
  FXL_DCHECK(!completed_);
  CallCallbacks(status);
}

Status PageEvictionManagerImpl::Completer::WaitUntilDone(
    coroutine::CoroutineHandler* handler) {
  if (completed_) {
    return status_;
  }

  auto sync_call_status =
      coroutine::SyncCall(handler, [this](fit::closure callback) {
        // SyncCall finishes its execution when the given |callback| is called.
        // To block the termination of |SyncCall| (and of |WaitUntilDone|), here
        // we push this |callback| in the vector of |callbacks_|. Once
        // |Complete| is called, we will call all of these callbacks, which will
        // eventually unblock all pending |WaitUntilDone| calls.
        callbacks_.push_back(std::move(callback));
      });
  if (sync_call_status == coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERNAL_ERROR;
  }
  return status_;
}

void PageEvictionManagerImpl::Completer::CallCallbacks(Status status) {
  if (completed_) {
    return;
  }
  completed_ = true;
  status_ = status;
  // We need to move the callbacks in the stack since calling any of the
  // them might lead to the deletion of this object, invalidating callbacks_.
  std::vector<fit::closure> callbacks = std::move(callbacks_);
  callbacks_.clear();
  for (const auto& callback : callbacks) {
    callback();
  }
}

PageEvictionManagerImpl::PageEvictionManagerImpl(
    async_dispatcher_t* dispatcher,
    coroutine::CoroutineService* coroutine_service,
    ledger::DetachedPath db_path)
    : dispatcher_(dispatcher),
      db_(dispatcher, db_path.SubPath({storage::kSerializationVersion,
                                       kPageUsageDbSerializationVersion})),
      coroutine_manager_(coroutine_service),
      weak_factory_(this) {}

PageEvictionManagerImpl::~PageEvictionManagerImpl() {}

Status PageEvictionManagerImpl::Init() {
  Status status = db_.Init();
  if (status != Status::OK) {
    return status;
  }

  // Marking pages as closed is a slow operation and we shouldn't wait for it to
  // return from initialization: Start marking the open pages as closed and
  // finalize the initialization completer when done.
  coroutine_manager_.StartCoroutine(
      [this](coroutine::CoroutineHandler* handler) {
        ExpiringToken token = NewExpiringToken();
        Status status = db_.MarkAllPagesClosed(handler);
        initialization_completer_.Complete(status);
      });
  return Status::OK;
}

void PageEvictionManagerImpl::SetDelegate(
    PageEvictionManager::Delegate* delegate) {
  FXL_DCHECK(delegate);
  FXL_DCHECK(!delegate_);
  delegate_ = delegate;
}

void PageEvictionManagerImpl::set_on_empty(fit::closure on_empty_callback) {
  on_empty_callback_ = std::move(on_empty_callback);
}

bool PageEvictionManagerImpl::IsEmpty() { return pending_operations_ == 0; }

void PageEvictionManagerImpl::TryCleanUp(fit::function<void(Status)> callback) {
  // TODO(nellyv): we should define some way to chose eviction policies.
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this](coroutine::CoroutineHandler* handler,
             fit::function<void(Status)> callback) mutable {
        ExpiringToken token = NewExpiringToken();
        Status status = initialization_completer_.WaitUntilDone(handler);
        if (LogOnInitializationError("TryCleanUp", status)) {
          callback(status);
          return;
        }
        // CanEvictPage is an expensive operation. Get the sorted list of all
        // pages first and call CanEvictPage exactly as many times as necessary.
        std::vector<PageUsageDb::PageInfo> pages;
        status = GetPagesByTimestamp(handler, &pages);
        if (status != Status::OK) {
          callback(status);
          return;
        }

        for (const auto& page_info : pages) {
          bool can_evict;
          Status status = CanEvictPage(handler, page_info.ledger_name,
                                       page_info.page_id, &can_evict);
          if (status == Status::PAGE_NOT_FOUND) {
            // The page was already removed, maybe from a previous call to
            // |TryCleanUp|. Mark it as evicted in the Page Usage DB.
            MarkPageEvicted(page_info.ledger_name, page_info.page_id);
            continue;
          }
          if (status != Status::OK) {
            callback(status);
            return;
          }
          if (can_evict) {
            auto sync_call_status = coroutine::SyncCall(
                handler,
                [this, ledger_name = page_info.ledger_name,
                 page_id = page_info.page_id](auto callback) {
                  EvictPage(ledger_name, page_id, std::move(callback));
                },
                &status);
            if (sync_call_status ==
                coroutine::ContinuationStatus::INTERRUPTED) {
              callback(Status::INTERNAL_ERROR);
            } else {
              callback(status);
            }
            return;
          }
        }
        callback(Status::OK);
      });
}

void PageEvictionManagerImpl::OnPageOpened(fxl::StringView ledger_name,
                                           storage::PageIdView page_id) {
  coroutine_manager_.StartCoroutine([this, ledger_name = ledger_name.ToString(),
                                     page_id = page_id.ToString()](
                                        coroutine::CoroutineHandler* handler) {
    ExpiringToken token = NewExpiringToken();
    Status status = initialization_completer_.WaitUntilDone(handler);
    if (LogOnInitializationError("OnPageOpened", status)) {
      return;
    }
    status = db_.MarkPageOpened(handler, ledger_name, page_id);
    LogOnPageUpdateError("mark page as opened", status, ledger_name, page_id);
  });
}

void PageEvictionManagerImpl::OnPageClosed(fxl::StringView ledger_name,
                                           storage::PageIdView page_id) {
  coroutine_manager_.StartCoroutine([this, ledger_name = ledger_name.ToString(),
                                     page_id = page_id.ToString()](
                                        coroutine::CoroutineHandler* handler) {
    ExpiringToken token = NewExpiringToken();
    Status status = initialization_completer_.WaitUntilDone(handler);
    if (LogOnInitializationError("OnPageClosed", status)) {
      return;
    }
    status = db_.MarkPageClosed(handler, ledger_name, page_id);
    LogOnPageUpdateError("mark page as closed", status, ledger_name, page_id);
  });
}

void PageEvictionManagerImpl::EvictPage(fxl::StringView ledger_name,
                                        storage::PageIdView page_id,
                                        fit::function<void(Status)> callback) {
  FXL_DCHECK(delegate_);
  // We cannot delete the page storage and mark the deletion atomically. We thus
  // delete the page first, and then mark it as evicted in Page Usage DB. If at
  // some point a page gets deleted, but marking fails, on the next attempt to
  // evict it we will get a |PAGE_NOT_FOUND| error, indicating we should remove
  // the entry then. Therefore, |PAGE_NOT_FOUND| errors are handled internally
  // and never returned to the callback.
  delegate_->DeletePageStorage(
      ledger_name, page_id,
      [this, ledger_name = ledger_name.ToString(), page_id = page_id.ToString(),
       callback = std::move(callback)](Status status) {
        // |PAGE_NOT_FOUND| is not an error, but it must have been handled
        // before we try to evict the page.
        FXL_DCHECK(status != Status::PAGE_NOT_FOUND);
        if (status == Status::OK) {
          MarkPageEvicted(std::move(ledger_name), std::move(page_id));
        }
        callback(status);
      });
}

Status PageEvictionManagerImpl::CanEvictPage(
    coroutine::CoroutineHandler* handler, fxl::StringView ledger_name,
    storage::PageIdView page_id, bool* can_evict) {
  FXL_DCHECK(delegate_);

  Status status;
  PageClosedAndSynced sync_state;
  auto sync_call_status =
      coroutine::SyncCall(handler,
                          [this, ledger_name = ledger_name.ToString(),
                           page_id = page_id.ToString()](auto callback) {
                            delegate_->PageIsClosedAndSynced(
                                ledger_name, page_id, std::move(callback));
                          },
                          &status, &sync_state);
  if (sync_call_status == coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERNAL_ERROR;
  }
  *can_evict = (sync_state == PageClosedAndSynced::YES);

  return status;
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

void PageEvictionManagerImpl::MarkPageEvicted(std::string ledger_name,
                                              storage::PageId page_id) {
  coroutine_manager_.StartCoroutine([this, ledger_name = std::move(ledger_name),
                                     page_id = std::move(page_id)](
                                        coroutine::CoroutineHandler* handler) {
    Status status = db_.MarkPageEvicted(handler, ledger_name, page_id);
    LogOnPageUpdateError("mark page as evicted", status, ledger_name, page_id);
  });
}

PageEvictionManagerImpl::ExpiringToken
PageEvictionManagerImpl::NewExpiringToken() {
  ++pending_operations_;
  return ExpiringToken(callback::MakeScoped(weak_factory_.GetWeakPtr(), [this] {
    --pending_operations_;
    // We need to post a task here: Tokens expire while a coroutine is being
    // executed, and if |on_empty_callback_| is executed directly, it might end
    // up deleting the PageEvictionManagerImpl object, which will delete the
    // |coroutine_manager_|.
    async::PostTask(dispatcher_,
                    callback::MakeScoped(weak_factory_.GetWeakPtr(), [this] {
                      if (on_empty_callback_ && pending_operations_ == 0) {
                        on_empty_callback_();
                      }
                    }));
  }));
}

}  // namespace ledger
