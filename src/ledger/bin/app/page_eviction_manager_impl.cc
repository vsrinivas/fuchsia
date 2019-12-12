// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_eviction_manager_impl.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include <algorithm>

#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/ledger_repository_impl.h"
#include "src/ledger/bin/app/page_eviction_policies.h"
#include "src/ledger/bin/app/page_usage_db.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/callback/waiter.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/ledger/lib/coroutine/coroutine_waiter.h"
#include "src/ledger/lib/logging/logging.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {
namespace {

// Logs an error message if the given |status| is not |OK| or |INTERRUPTED|.
void LogOnPageUpdateError(absl::string_view operation_description, Status status,
                          absl::string_view ledger_name, storage::PageIdView page_id) {
  // Don't print an error on |INTERRUPED|: it means that the operation was
  // interrupted, because PageEvictionManagerImpl was destroyed before being
  // empty.
  if (status != Status::OK && status != Status::INTERRUPTED) {
    LEDGER_LOG(ERROR) << "Failed to " << operation_description
                      << " in PageUsage DB. Status: " << fidl::ToUnderlying(status)
                      << ". Ledger name: " << ledger_name
                      << ". Page ID: " << convert::ToHex(page_id);
  }
}
}  // namespace

PageEvictionManagerImpl::PageEvictionManagerImpl(Environment* environment, PageUsageDb* db)
    : environment_(environment), db_(db), coroutine_manager_(environment_->coroutine_service()) {}

PageEvictionManagerImpl::~PageEvictionManagerImpl() = default;

void PageEvictionManagerImpl::SetDelegate(PageEvictionManager::Delegate* delegate) {
  LEDGER_DCHECK(delegate);
  LEDGER_DCHECK(!delegate_);
  delegate_ = delegate;
}

void PageEvictionManagerImpl::SetOnDiscardable(fit::closure on_discardable) {
  token_manager_.SetOnDiscardable(std::move(on_discardable));
}

bool PageEvictionManagerImpl::IsDiscardable() const { return token_manager_.IsDiscardable(); }

void PageEvictionManagerImpl::TryEvictPages(PageEvictionPolicy* policy,
                                            fit::function<void(Status)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback), [this, policy](coroutine::CoroutineHandler* handler,
                                          fit::function<void(Status)> callback) mutable {
        ExpiringToken token = token_manager_.CreateToken();
        std::unique_ptr<storage::Iterator<const PageInfo>> pages_it;
        Status status = db_->GetPages(handler, &pages_it);
        if (status != Status::OK) {
          callback(status);
          return;
        }
        policy->SelectAndEvict(std::move(pages_it), std::move(callback));
      });
}

void PageEvictionManagerImpl::MarkPageOpened(absl::string_view ledger_name,
                                             storage::PageIdView page_id) {
  coroutine_manager_.StartCoroutine(
      [this, ledger_name = convert::ToString(ledger_name),
       page_id = convert::ToString(page_id)](coroutine::CoroutineHandler* handler) {
        ExpiringToken token = token_manager_.CreateToken();
        Status status = db_->MarkPageOpened(handler, ledger_name, page_id);
        LogOnPageUpdateError("mark page as opened", status, ledger_name, page_id);
      });
}

void PageEvictionManagerImpl::MarkPageClosed(absl::string_view ledger_name,
                                             storage::PageIdView page_id) {
  coroutine_manager_.StartCoroutine(
      [this, ledger_name = convert::ToString(ledger_name),
       page_id = convert::ToString(page_id)](coroutine::CoroutineHandler* handler) {
        ExpiringToken token = token_manager_.CreateToken();
        Status status = db_->MarkPageClosed(handler, ledger_name, page_id);
        LogOnPageUpdateError("mark page as closed", status, ledger_name, page_id);
      });
}

void PageEvictionManagerImpl::TryEvictPage(absl::string_view ledger_name,
                                           storage::PageIdView page_id,
                                           PageEvictionCondition condition,
                                           fit::function<void(Status, PageWasEvicted)> callback) {
  coroutine_manager_.StartCoroutine(
      [callback = std::move(callback)](Status status, PageWasEvicted was_evicted) {
        // TODO(fxb/42226): replace this stop-gap interception of INTERRUPTED status by a more
        // generic solution.
        if (status != Status::INTERRUPTED) {
          callback(status, was_evicted);
        }
      },
      [this, ledger_name = convert::ToString(ledger_name), page_id = convert::ToString(page_id),
       condition](coroutine::CoroutineHandler* handler) mutable {
        ExpiringToken token = token_manager_.CreateToken();
        PageWasEvicted was_evicted;
        Status status =
            SynchronousTryEvictPage(handler, ledger_name, page_id, condition, &was_evicted);
        return std::make_tuple(status, was_evicted);
      });
}

void PageEvictionManagerImpl::EvictPage(absl::string_view ledger_name, storage::PageIdView page_id,
                                        fit::function<void(Status)> callback) {
  LEDGER_DCHECK(delegate_);
  // We cannot delete the page storage and mark the deletion atomically. We thus
  // delete the page first, and then mark it as evicted in Page Usage DB.
  delegate_->DeletePageStorage(
      ledger_name, page_id,
      [this, ledger_name = convert::ToString(ledger_name), page_id = convert::ToString(page_id),
       callback = std::move(callback)](Status status) mutable {
        // |PAGE_NOT_FOUND| is not an error, but it must have been handled
        // before we try to evict the page.
        LEDGER_DCHECK(status != Status::PAGE_NOT_FOUND);
        if (status == Status::OK) {
          MarkPageEvicted(std::move(ledger_name), std::move(page_id));
        }
        callback(status);
      });
}

Status PageEvictionManagerImpl::CanEvictPage(coroutine::CoroutineHandler* handler,
                                             absl::string_view ledger_name,
                                             storage::PageIdView page_id, bool* can_evict) {
  LEDGER_DCHECK(delegate_);

  auto waiter = fxl::MakeRefCounted<Waiter<Status, PagePredicateResult>>(Status::OK);

  delegate_->PageIsClosedAndSynced(ledger_name, page_id, waiter->NewCallback());
  delegate_->PageIsClosedOfflineAndEmpty(ledger_name, page_id, waiter->NewCallback());

  Status status;
  std::vector<PagePredicateResult> can_evict_states;
  auto sync_call_status = coroutine::Wait(handler, std::move(waiter), &status, &can_evict_states);
  if (sync_call_status == coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  RETURN_ON_ERROR(status);
  LEDGER_DCHECK(can_evict_states.size() == 2);
  // Receiving status |PAGE_OPENED| means that the page was opened during the
  // query. If either result is |PAGE_OPENED| the page cannot be evicted, as the
  // result of the other might be invalid at this point.
  *can_evict =
      std::any_of(can_evict_states.begin(), can_evict_states.end(),
                  [](PagePredicateResult result) { return result == PagePredicateResult::YES; }) &&
      std::none_of(
          can_evict_states.begin(), can_evict_states.end(),
          [](PagePredicateResult result) { return result == PagePredicateResult::PAGE_OPENED; });

  return Status::OK;
}

Status PageEvictionManagerImpl::CanEvictEmptyPage(coroutine::CoroutineHandler* handler,
                                                  absl::string_view ledger_name,
                                                  storage::PageIdView page_id, bool* can_evict) {
  LEDGER_DCHECK(delegate_);

  Status status;
  PagePredicateResult empty_state;
  auto sync_call_status = coroutine::SyncCall(
      handler,
      [this, ledger_name = convert::ToString(ledger_name),
       page_id = convert::ToString(page_id)](auto callback) {
        delegate_->PageIsClosedOfflineAndEmpty(ledger_name, page_id, std::move(callback));
      },
      &status, &empty_state);
  if (sync_call_status == coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  *can_evict = (empty_state == PagePredicateResult::YES);
  return status;
}

void PageEvictionManagerImpl::MarkPageEvicted(std::string ledger_name, storage::PageId page_id) {
  coroutine_manager_.StartCoroutine(
      [this, ledger_name = std::move(ledger_name),
       page_id = std::move(page_id)](coroutine::CoroutineHandler* handler) {
        Status status = db_->MarkPageEvicted(handler, ledger_name, page_id);
        LogOnPageUpdateError("mark page as evicted", status, ledger_name, page_id);
      });
}

Status PageEvictionManagerImpl::SynchronousTryEvictPage(coroutine::CoroutineHandler* handler,
                                                        std::string ledger_name,
                                                        storage::PageId page_id,
                                                        PageEvictionCondition condition,
                                                        PageWasEvicted* was_evicted) {
  bool can_evict;
  Status status;
  switch (condition) {
    case IF_EMPTY:
      status = CanEvictEmptyPage(handler, ledger_name, page_id, &can_evict);
      break;
    case IF_POSSIBLE:
      status = CanEvictPage(handler, ledger_name, page_id, &can_evict);
  }
  if (status == Status::PAGE_NOT_FOUND) {
    // |PAGE_NOT_FOUND| is not an error: It is possible that the page was
    // removed in a previous run, but for some reason marking failed (e.g.
    // Ledger was shut down before the operation finished). Mark the page as
    // evicted in Page Usage DB, and set |was_evicted| to false, since the page
    // was not actually evicted here.
    MarkPageEvicted(ledger_name, page_id);
    *was_evicted = PageWasEvicted(false);
    return Status::OK;
  }
  if (status != Status::OK || !can_evict) {
    *was_evicted = PageWasEvicted(false);
    return status;
  }

  // At this point, the requirements for calling |EvictPage| are met: the page
  // exists and can be evicted.
  auto sync_call_status = coroutine::SyncCall(
      handler,
      [this, ledger_name = std::move(ledger_name), page_id = std::move(page_id)](auto callback) {
        EvictPage(ledger_name, page_id, std::move(callback));
      },
      &status);
  if (sync_call_status == coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  *was_evicted = PageWasEvicted(status == Status::OK);
  return status;
}

}  // namespace ledger
