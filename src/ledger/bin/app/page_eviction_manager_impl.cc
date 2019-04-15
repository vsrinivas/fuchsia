// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_eviction_manager_impl.h"

#include <lib/async/cpp/task.h>
#include <lib/callback/waiter.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include <algorithm>

#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/ledger_repository_impl.h"
#include "src/ledger/bin/app/page_usage_db.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/ledger/lib/coroutine/coroutine_waiter.h"
#include "src/lib/files/directory.h"
#include "src/lib/fxl/strings/concatenate.h"

namespace ledger {
namespace {

// Logs an error message if the given |status| is not |OK| or |INTERRUPTED|.
void LogOnPageUpdateError(fxl::StringView operation_description,
                          storage::Status status, fxl::StringView ledger_name,
                          storage::PageIdView page_id) {
  // Don't print an error on |INTERRUPED|: it means that the operation was
  // interrupted, because PageEvictionManagerImpl was destroyed before being
  // empty.
  if (status != storage::Status::OK &&
      status != storage::Status::INTERRUPTED) {
    FXL_LOG(ERROR) << "Failed to " << operation_description
                   << " in PageUsage DB. storage::Status: "
                   << fidl::ToUnderlying(status)
                   << ". Ledger name: " << ledger_name
                   << ". Page ID: " << convert::ToHex(page_id);
  }
}

// If the given |status| is not |OK| or |INTERRUPTED|, logs an error message on
// failure to initialize. Returns true in case of error; false otherwise.
bool LogOnInitializationError(fxl::StringView operation_description,
                              storage::Status status) {
  if (status != storage::Status::OK) {
    if (status != storage::Status::INTERRUPTED) {
      FXL_LOG(ERROR) << operation_description
                     << " failed because of initialization error: "
                     << fidl::ToUnderlying(status);
    }
    return true;
  }
  return false;
}

}  // namespace

PageEvictionManagerImpl::Completer::Completer() {}

PageEvictionManagerImpl::Completer::~Completer() {
  // We should not call the callbacks: they are SyncCall callbacks, so when we
  // drop them the caller will receive |INTERRUPTED|.
}

void PageEvictionManagerImpl::Completer::Complete(storage::Status status) {
  FXL_DCHECK(!completed_);
  // If we get |INTERRUPTED| here, it means the caller did not return as soon as
  // it received |INTERRUPTED|.
  FXL_DCHECK(status != storage::Status::INTERRUPTED);
  CallCallbacks(status);
}

storage::Status PageEvictionManagerImpl::Completer::WaitUntilDone(
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
    return storage::Status::INTERRUPTED;
  }
  return status_;
}

void PageEvictionManagerImpl::Completer::CallCallbacks(storage::Status status) {
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

void PageEvictionManagerImpl::Completer::Cancel() {
  FXL_DCHECK(!completed_);
  completed_ = true;
  status_ = storage::Status::INTERRUPTED;
  callbacks_.clear();
}

PageEvictionManagerImpl::PageEvictionManagerImpl(Environment* environment,
                                                 storage::DbFactory* db_factory,
                                                 DetachedPath db_path)
    : environment_(environment),
      db_factory_(db_factory),
      db_path_(db_path.SubPath(kPageUsageDbSerializationVersion)),
      coroutine_manager_(environment_->coroutine_service()),
      weak_factory_(this) {}

PageEvictionManagerImpl::~PageEvictionManagerImpl() {}

void PageEvictionManagerImpl::Init() {
  // Initializing the DB and marking pages as closed are slow operations and we
  // shouldn't wait for them to finish, before returning from initialization:
  // Start these operations and finalize the initialization completer when done.
  coroutine_manager_.StartCoroutine([this](
                                        coroutine::CoroutineHandler* handler) {
    ExpiringToken token = NewExpiringToken();
    if (!files::CreateDirectoryAt(db_path_.root_fd(), db_path_.path())) {
      initialization_completer_.Complete(storage::Status::IO_ERROR);
      return;
    }
    storage::Status status;
    std::unique_ptr<storage::Db> db_instance;
    if (coroutine::SyncCall(
            handler,
            [this](fit::function<void(storage::Status,
                                      std::unique_ptr<storage::Db>)>
                       callback) {
              db_factory_->GetOrCreateDb(
                  std::move(db_path_), storage::DbFactory::OnDbNotFound::CREATE,
                  std::move(callback));
            },
            &status,
            &db_instance) == coroutine::ContinuationStatus::INTERRUPTED) {
      initialization_completer_.Cancel();
      return;
    }
    if (status != storage::Status::OK) {
      initialization_completer_.Complete(status);
      return;
    }
    db_ = std::make_unique<PageUsageDb>(environment_->clock(),
                                        std::move(db_instance));
    status = db_->MarkAllPagesClosed(handler);
    if (status == storage::Status::INTERRUPTED) {
      initialization_completer_.Cancel();
      return;
    }
    initialization_completer_.Complete(status);
  });
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

void PageEvictionManagerImpl::TryEvictPages(
    PageEvictionPolicy* policy, fit::function<void(storage::Status)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, policy](coroutine::CoroutineHandler* handler,
                     fit::function<void(storage::Status)> callback) mutable {
        ExpiringToken token = NewExpiringToken();
        storage::Status status =
            initialization_completer_.WaitUntilDone(handler);
        if (LogOnInitializationError("TryEvictPages", status)) {
          callback(status);
          return;
        }
        std::unique_ptr<storage::Iterator<const PageInfo>> pages_it;
        status = db_->GetPages(handler, &pages_it);
        if (status != storage::Status::OK) {
          callback(status);
          return;
        }
        policy->SelectAndEvict(std::move(pages_it), std::move(callback));
      });
}

void PageEvictionManagerImpl::MarkPageOpened(fxl::StringView ledger_name,
                                             storage::PageIdView page_id) {
  coroutine_manager_.StartCoroutine([this, ledger_name = ledger_name.ToString(),
                                     page_id = page_id.ToString()](
                                        coroutine::CoroutineHandler* handler) {
    ExpiringToken token = NewExpiringToken();
    storage::Status status = initialization_completer_.WaitUntilDone(handler);
    if (LogOnInitializationError("MarkPageOpened", status)) {
      return;
    }
    status = db_->MarkPageOpened(handler, ledger_name, page_id);
    LogOnPageUpdateError("mark page as opened", status, ledger_name, page_id);
  });
}

void PageEvictionManagerImpl::MarkPageClosed(fxl::StringView ledger_name,
                                             storage::PageIdView page_id) {
  coroutine_manager_.StartCoroutine([this, ledger_name = ledger_name.ToString(),
                                     page_id = page_id.ToString()](
                                        coroutine::CoroutineHandler* handler) {
    ExpiringToken token = NewExpiringToken();
    storage::Status status = initialization_completer_.WaitUntilDone(handler);
    if (LogOnInitializationError("MarkPageClosed", status)) {
      return;
    }
    status = db_->MarkPageClosed(handler, ledger_name, page_id);
    LogOnPageUpdateError("mark page as closed", status, ledger_name, page_id);
  });
}

void PageEvictionManagerImpl::TryEvictPage(
    fxl::StringView ledger_name, storage::PageIdView page_id,
    PageEvictionCondition condition,
    fit::function<void(storage::Status, PageWasEvicted)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, ledger_name = ledger_name.ToString(), page_id = page_id.ToString(),
       condition](coroutine::CoroutineHandler* handler,
                  fit::function<void(storage::Status, PageWasEvicted)>
                      callback) mutable {
        ExpiringToken token = NewExpiringToken();
        storage::Status status =
            initialization_completer_.WaitUntilDone(handler);
        if (LogOnInitializationError("TryEvictPage", status)) {
          callback(status, PageWasEvicted(false));
          return;
        }
        PageWasEvicted was_evicted;
        status = SynchronousTryEvictPage(handler, ledger_name, page_id,
                                         condition, &was_evicted);
        callback(status, was_evicted);
      });
}

void PageEvictionManagerImpl::EvictPage(
    fxl::StringView ledger_name, storage::PageIdView page_id,
    fit::function<void(storage::Status)> callback) {
  FXL_DCHECK(delegate_);
  // We cannot delete the page storage and mark the deletion atomically. We thus
  // delete the page first, and then mark it as evicted in Page Usage DB.
  delegate_->DeletePageStorage(
      ledger_name, page_id,
      [this, ledger_name = ledger_name.ToString(), page_id = page_id.ToString(),
       callback = std::move(callback)](storage::Status status) mutable {
        // |PAGE_NOT_FOUND| is not an error, but it must have been handled
        // before we try to evict the page.
        FXL_DCHECK(status != storage::Status::PAGE_NOT_FOUND);
        if (status == storage::Status::OK) {
          MarkPageEvicted(std::move(ledger_name), std::move(page_id));
        }
        callback(status);
      });
}

storage::Status PageEvictionManagerImpl::CanEvictPage(
    coroutine::CoroutineHandler* handler, fxl::StringView ledger_name,
    storage::PageIdView page_id, bool* can_evict) {
  FXL_DCHECK(delegate_);

  auto waiter = fxl::MakeRefCounted<
      callback::Waiter<storage::Status, PagePredicateResult>>(
      storage::Status::OK);

  delegate_->PageIsClosedAndSynced(ledger_name, page_id, waiter->NewCallback());
  delegate_->PageIsClosedOfflineAndEmpty(ledger_name, page_id,
                                         waiter->NewCallback());

  storage::Status status;
  std::vector<PagePredicateResult> can_evict_states;
  auto sync_call_status =
      coroutine::Wait(handler, std::move(waiter), &status, &can_evict_states);
  if (sync_call_status == coroutine::ContinuationStatus::INTERRUPTED) {
    return storage::Status::INTERRUPTED;
  }
  if (status != storage::Status::OK) {
    return status;
  }
  FXL_DCHECK(can_evict_states.size() == 2);
  // Receiving status |PAGE_OPENED| means that the page was opened during the
  // query. If either result is |PAGE_OPENED| the page cannot be evicted, as the
  // result of the other might be invalid at this point.
  *can_evict = std::any_of(can_evict_states.begin(), can_evict_states.end(),
                           [](PagePredicateResult result) {
                             return result == PagePredicateResult::YES;
                           }) &&
               std::none_of(can_evict_states.begin(), can_evict_states.end(),
                            [](PagePredicateResult result) {
                              return result == PagePredicateResult::PAGE_OPENED;
                            });

  return storage::Status::OK;
}

storage::Status PageEvictionManagerImpl::CanEvictEmptyPage(
    coroutine::CoroutineHandler* handler, fxl::StringView ledger_name,
    storage::PageIdView page_id, bool* can_evict) {
  FXL_DCHECK(delegate_);

  storage::Status status;
  PagePredicateResult empty_state;
  auto sync_call_status = coroutine::SyncCall(
      handler,
      [this, ledger_name = ledger_name.ToString(),
       page_id = page_id.ToString()](auto callback) {
        delegate_->PageIsClosedOfflineAndEmpty(ledger_name, page_id,
                                               std::move(callback));
      },
      &status, &empty_state);
  if (sync_call_status == coroutine::ContinuationStatus::INTERRUPTED) {
    return storage::Status::INTERRUPTED;
  }
  *can_evict = (empty_state == PagePredicateResult::YES);
  return status;
}

void PageEvictionManagerImpl::MarkPageEvicted(std::string ledger_name,
                                              storage::PageId page_id) {
  coroutine_manager_.StartCoroutine([this, ledger_name = std::move(ledger_name),
                                     page_id = std::move(page_id)](
                                        coroutine::CoroutineHandler* handler) {
    storage::Status status =
        db_->MarkPageEvicted(handler, ledger_name, page_id);
    LogOnPageUpdateError("mark page as evicted", status, ledger_name, page_id);
  });
}

storage::Status PageEvictionManagerImpl::SynchronousTryEvictPage(
    coroutine::CoroutineHandler* handler, std::string ledger_name,
    storage::PageId page_id, PageEvictionCondition condition,
    PageWasEvicted* was_evicted) {
  bool can_evict;
  storage::Status status;
  switch (condition) {
    case IF_EMPTY:
      status = CanEvictEmptyPage(handler, ledger_name, page_id, &can_evict);
      break;
    case IF_POSSIBLE:
      status = CanEvictPage(handler, ledger_name, page_id, &can_evict);
  }
  if (status == storage::Status::PAGE_NOT_FOUND) {
    // |PAGE_NOT_FOUND| is not an error: It is possible that the page was
    // removed in a previous run, but for some reason marking failed (e.g.
    // Ledger was shut down before the operation finished). Mark the page as
    // evicted in Page Usage DB, and set |was_evicted| to false, since the page
    // was not actually evicted here.
    MarkPageEvicted(ledger_name, page_id);
    *was_evicted = PageWasEvicted(false);
    return storage::Status::OK;
  }
  if (status != storage::Status::OK || !can_evict) {
    *was_evicted = PageWasEvicted(false);
    return status;
  }

  // At this point, the requirements for calling |EvictPage| are met: the page
  // exists and can be evicted.
  auto sync_call_status = coroutine::SyncCall(
      handler,
      [this, ledger_name = std::move(ledger_name),
       page_id = std::move(page_id)](auto callback) {
        EvictPage(ledger_name, page_id, std::move(callback));
      },
      &status);
  if (sync_call_status == coroutine::ContinuationStatus::INTERRUPTED) {
    return storage::Status::INTERRUPTED;
  }
  *was_evicted = PageWasEvicted(status == storage::Status::OK);
  return status;
}

ExpiringToken PageEvictionManagerImpl::NewExpiringToken() {
  ++pending_operations_;
  return ExpiringToken(callback::MakeScoped(weak_factory_.GetWeakPtr(), [this] {
    --pending_operations_;
    // We need to post a task here: Tokens expire while a coroutine is being
    // executed, and if |on_empty_callback_| is executed directly, it might end
    // up deleting the PageEvictionManagerImpl object, which will delete the
    // |coroutine_manager_|.
    async::PostTask(environment_->dispatcher(),
                    callback::MakeScoped(weak_factory_.GetWeakPtr(), [this] {
                      if (on_empty_callback_ && pending_operations_ == 0) {
                        on_empty_callback_();
                      }
                    }));
  }));
}

}  // namespace ledger
