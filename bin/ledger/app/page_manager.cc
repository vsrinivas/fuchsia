// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/page_manager.h"

#include <algorithm>

#include "lib/fxl/logging.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "peridot/bin/ledger/app/page_utils.h"
#include "peridot/bin/ledger/encryption/primitives/rand.h"
#include "peridot/bin/ledger/storage/impl/number_serialization.h"

namespace ledger {

PageManager::PageManager(
    Environment* environment,
    std::unique_ptr<storage::PageStorage> page_storage,
    std::unique_ptr<cloud_sync::PageSyncContext> page_sync_context,
    std::unique_ptr<MergeResolver> merge_resolver,
    PageManager::PageStorageState state,
    fxl::TimeDelta sync_timeout)
    : environment_(environment),
      page_storage_(std::move(page_storage)),
      page_sync_context_(std::move(page_sync_context)),
      merge_resolver_(std::move(merge_resolver)),
      sync_timeout_(sync_timeout),
      task_runner_(environment->main_runner()) {
  pages_.set_on_empty([this] { CheckEmpty(); });
  snapshots_.set_on_empty([this] { CheckEmpty(); });
  page_debug_bindings_.set_on_empty_set_handler([this] { CheckEmpty(); });

  if (page_sync_context_) {
    page_sync_context_->page_sync->SetSyncWatcher(&watchers_);
    page_sync_context_->page_sync->SetOnIdle([this] { CheckEmpty(); });
    page_sync_context_->page_sync->SetOnBacklogDownloaded(
        [this] { OnSyncBacklogDownloaded(); });
    page_sync_context_->page_sync->Start();
    if (state == PageManager::PageStorageState::NEW) {
      // The page storage was created locally. We wait a bit in order to get the
      // initial state from the network before accepting requests.
      task_runner_.PostDelayedTask(
          [this] {
            if (!sync_backlog_downloaded_) {
              FXL_LOG(INFO) << "Initial sync will continue in background, "
                            << "in the meantime binding to local page data "
                            << "(might be stale or empty).";
              OnSyncBacklogDownloaded();
            }
          },
          sync_timeout_);
    } else {
      sync_backlog_downloaded_ = true;
    }
  } else {
    sync_backlog_downloaded_ = true;
  }
  merge_resolver_->set_on_empty([this] { CheckEmpty(); });
  merge_resolver_->SetPageManager(this);
}

PageManager::~PageManager() {
  for (const auto& request : page_requests_) {
    request.second(Status::INTERNAL_ERROR);
  }
  page_requests_.clear();
}

void PageManager::BindPage(fidl::InterfaceRequest<Page> page_request,
                           std::function<void(Status)> on_done) {
  if (sync_backlog_downloaded_) {
    pages_
        .emplace(environment_->coroutine_service(), this, page_storage_.get(),
                 merge_resolver_.get(), std::move(page_request), &watchers_)
        .Init(std::move(on_done));
    return;
  }
  page_requests_.emplace_back(std::move(page_request), std::move(on_done));
}

void PageManager::BindPageDebug(fidl::InterfaceRequest<PageDebug> page_debug,
                                std::function<void(Status)> callback) {
  page_debug_bindings_.AddBinding(this, std::move(page_debug));
  callback(Status::OK);
}

void PageManager::BindPageSnapshot(
    std::unique_ptr<const storage::Commit> commit,
    fidl::InterfaceRequest<PageSnapshot> snapshot_request,
    std::string key_prefix) {
  snapshots_.emplace(std::move(snapshot_request), page_storage_.get(),
                     std::move(commit), std::move(key_prefix));
}

ReferencePtr PageManager::CreateReference(
    storage::ObjectIdentifier object_identifier) {
  uint64_t index;
  encryption::RandBytes(&index, sizeof(index));
  FXL_DCHECK(references_.find(index) == references_.end());
  references_[index] = std::move(object_identifier);
  ReferencePtr reference = Reference::New();
  reference->opaque_id = convert::ToArray(storage::SerializeNumber(index));
  return reference;
}

Status PageManager::ResolveReference(
    ReferencePtr reference,
    storage::ObjectIdentifier* object_identifier) {
  if (reference->opaque_id.size() != sizeof(uint64_t)) {
    return Status::REFERENCE_NOT_FOUND;
  }
  uint64_t index = storage::DeserializeNumber<uint64_t>(
      convert::ToStringView(reference->opaque_id));
  auto iterator = references_.find(index);
  if (iterator == references_.end()) {
    return Status::REFERENCE_NOT_FOUND;
  }
  *object_identifier = iterator->second;
  return Status::OK;
}

void PageManager::CheckEmpty() {
  if (on_empty_callback_ && pages_.empty() && snapshots_.empty() &&
      page_requests_.empty() && merge_resolver_->IsEmpty() &&
      (!page_sync_context_ || page_sync_context_->page_sync->IsIdle()) &&
      page_debug_bindings_.size() == 0) {
    on_empty_callback_();
  }
}

void PageManager::OnSyncBacklogDownloaded() {
  if (sync_backlog_downloaded_) {
    FXL_LOG(INFO) << "Initial sync in background finished. "
                  << "Clients will receive a change notification.";
  }
  sync_backlog_downloaded_ = true;
  for (auto& page_request : page_requests_) {
    BindPage(std::move(page_request.first), std::move(page_request.second));
  }
  page_requests_.clear();
}

void PageManager::GetHeadCommitsIds(const GetHeadCommitsIdsCallback& callback) {
  page_storage_->GetHeadCommitIds(
      [callback](storage::Status status, std::vector<storage::CommitId> heads) {
        fidl::Array<fidl::Array<uint8_t>> result =
            fidl::Array<fidl::Array<uint8_t>>::New(0);
        for (const auto& head : heads)
          result.push_back(convert::ToArray(head));

        callback(PageUtils::ConvertStatus(status, Status::INVALID_ARGUMENT),
                 std::move(result));
      });
}

void PageManager::GetSnapshot(
    fidl::Array<uint8_t> commit_id,
    fidl::InterfaceRequest<PageSnapshot> snapshot_request,
    const GetSnapshotCallback& callback) {
  page_storage_->GetCommit(
      convert::ToStringView(commit_id),
      fxl::MakeCopyable(
          [this, snapshot_request = std::move(snapshot_request), callback](
              storage::Status status,
              std::unique_ptr<const storage::Commit> commit) mutable {
            if (status == storage::Status::OK) {
              BindPageSnapshot(std::move(commit), std::move(snapshot_request),
                               "");
            }
            callback(
                PageUtils::ConvertStatus(status, Status::INVALID_ARGUMENT));
          }));
}

void PageManager::GetCommit(fidl::Array<uint8_t> commit_id,
                            const GetCommitCallback& callback) {
  page_storage_->GetCommit(
      convert::ToStringView(commit_id),
      fxl::MakeCopyable(
          [callback = std::move(callback)](
              storage::Status status,
              std::unique_ptr<const storage::Commit> commit) mutable {
            ledger::CommitPtr commit_struct = NULL;
            if (status == storage::Status::OK) {
              commit_struct = ledger::Commit::New();
              commit_struct->commit_id = convert::ToArray(commit->GetId());
              commit_struct->parents_ids =
                  fidl::Array<fidl::Array<uint8_t>>::New(0);
              for (storage::CommitIdView parent : commit->GetParentIds()) {
                commit_struct->parents_ids.push_back(convert::ToArray(parent));
              }
              commit_struct->timestamp = commit->GetTimestamp();
              commit_struct->generation = commit->GetGeneration();
            }
            callback(PageUtils::ConvertStatus(status, Status::INVALID_ARGUMENT),
                     std::move(commit_struct));
          }));
}

}  // namespace ledger
