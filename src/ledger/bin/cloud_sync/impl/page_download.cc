// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/page_download.h"

#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>

#include <optional>

#include "src/ledger/bin/cloud_sync/impl/constants.h"
#include "src/ledger/bin/cloud_sync/impl/entry_payload_encoding.h"
#include "src/ledger/bin/cloud_sync/impl/status.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/data_source.h"
#include "src/ledger/bin/storage/public/read_data_source.h"
#include "src/ledger/lib/encoding/encoding.h"
#include "src/lib/callback/waiter.h"
#include "src/lib/fxl/strings/concatenate.h"

namespace cloud_sync {
namespace {
DownloadSyncState GetMergedState(DownloadSyncState commit_state, int current_get_object_calls) {
  if (commit_state != DOWNLOAD_IDLE) {
    return commit_state;
  }
  return current_get_object_calls == 0 ? DOWNLOAD_IDLE : DOWNLOAD_IN_PROGRESS;
}

// Normalizes a diff so that the cloud cannot learn anything from knowing whether it applies
// successfully or not.
//
// We fail for diffs that the cloud would know are invalid. We can learn here that some other diffs
// are invalid, eg. those that insert a key twice, but it would be to risky to reject them here: we
// do not want the cloud to be able to distinguish between a failure due to having duplicate keys
// and a failure due to not getting the expected tree after applying the diff, and it's easier to do
// this if we follow the same code/error handling path in those two cases.
//
// We rely on diffs being applied strictly: deletions are only valid if they matchh precisely the
// entry present in the tree, and the diff is rejected otherwise. Similarly, insertions are rejected
// if the key exists instead of being turned into updates.
//
// Some parts of the diff might have been shuffled before being sent. For simplicity, we completely
// ignore the order in which changes have been sent. Once we've matched and simplified insertions
// and deletions based on the entry id (which is non-secret), the diff can only apply successfully
// if all deletions delete things that are in the base version, and all insertions insert things
// that are in the target version, and there is no entry with this key in the base version or it has
// been deleted. If that's the case, it will apply successfully if we apply deletions at a given key
// before insertions at this key. We sort by key because this is expected by
// |storage::btree::ApplyChangesFromCloud|.
bool NormalizeDiff(std::vector<storage::EntryChange>* changes) {
  auto compare_by_entryid = [](const storage::Entry& lhs, const storage::Entry& rhs) {
    return lhs.entry_id < rhs.entry_id;
  };
  // To each entry id, we associate the first entry we found with this id, and the count of entries
  // with this id. Inserted entries are counted as +1, deleted entries as -1.
  std::map<storage::Entry, int64_t, decltype(compare_by_entryid)> entries(compare_by_entryid);

  for (auto& change : *changes) {
    entries[std::move(change.entry)] += change.deleted ? -1 : +1;
  }
  // Changes have been invalidated by the move.
  changes->clear();

  // Serialize the map back to a vector. We expect all counts to be 0, +1 or -1: other diffs will
  // not apply successfully and can be rejected now because the count is only based on the
  // non-secret entry ids.
  while (!entries.empty()) {
    auto node = entries.extract(entries.begin());
    if (node.mapped() == 0) {
      // Insertions and deletions cancel.
    } else if (node.mapped() == -1) {
      // Only one deletion remains.
      changes->push_back({std::move(node.key()), true});
    } else if (node.mapped() == 1) {
      // Only one insertion remains.
      changes->push_back({std::move(node.key()), false});
    } else {
      // Multiple insertions or deletions remain, the diff is invalid. Failing here is OK, because
      // we only depend on information known to the cloud.
      return false;
    }
  }

  // Sort the vector by entry name, putting deletions before insertions.
  auto compare_by_key_deletions_first = [](const storage::EntryChange& lhs,
                                           const storage::EntryChange& rhs) {
    // We want deleted = true before deleted = false, but false < true.
    return std::forward_as_tuple(lhs.entry.key, !lhs.deleted) <
           std::forward_as_tuple(rhs.entry.key, !rhs.deleted);
  };
  std::sort(changes->begin(), changes->end(), compare_by_key_deletions_first);

  return true;
}

}  // namespace

PageDownload::PageDownload(callback::ScopedTaskRunner* task_runner, storage::PageStorage* storage,
                           encryption::EncryptionService* encryption_service,
                           cloud_provider::PageCloudPtr* page_cloud, Delegate* delegate,
                           std::unique_ptr<backoff::Backoff> backoff)
    : task_runner_(task_runner),
      storage_(storage),
      encryption_service_(encryption_service),
      page_cloud_(page_cloud),
      delegate_(delegate),
      backoff_(std::move(backoff)),
      log_prefix_(
          fxl::Concatenate({"Page ", convert::ToHex(storage->GetId()), " download sync: "})),
      watcher_binding_(this),
      weak_factory_(this) {}

PageDownload::~PageDownload() = default;

void PageDownload::StartDownload() {
  SetCommitState(DOWNLOAD_BACKLOG);

  // Retrieve the server-side timestamp of the last commit we received.
  storage_->GetSyncMetadata(
      kTimestampKey,
      task_runner_->MakeScoped([this](ledger::Status status, std::string last_commit_token_id) {
        // NOT_FOUND means that we haven't persisted the state yet, e.g. because
        // we haven't received any remote commits yet. In this case an empty
        // timestamp is the right value.
        if (status != ledger::Status::OK && status != ledger::Status::INTERNAL_NOT_FOUND) {
          HandleDownloadCommitError("Failed to retrieve the sync metadata.");
          return;
        }
        if (last_commit_token_id.empty()) {
          FXL_VLOG(1) << log_prefix_ << "starting sync for the first time, "
                      << "retrieving all remote commits";
        } else {
          // TODO(ppi): print the timestamp out as human-readable wall time.
          FXL_VLOG(1) << log_prefix_ << "starting sync again, "
                      << "retrieving commits uploaded after: " << last_commit_token_id;
        }

        std::unique_ptr<cloud_provider::PositionToken> position_token;
        if (!last_commit_token_id.empty()) {
          position_token = std::make_unique<cloud_provider::PositionToken>();
          position_token->opaque_id = convert::ToArray(last_commit_token_id);
        }
        (*page_cloud_)
            ->GetCommits(
                std::move(position_token),
                [this](cloud_provider::Status cloud_status,
                       std::unique_ptr<cloud_provider::CommitPack> commit_pack,
                       std::unique_ptr<cloud_provider::PositionToken> position_token) {
                  if (cloud_status != cloud_provider::Status::OK) {
                    // Fetching the remote commits failed, schedule a retry.
                    FXL_LOG(WARNING)
                        << log_prefix_ << "fetching the remote commits failed due to a "
                        << "connection error, status: " << fidl::ToUnderlying(cloud_status)
                        << ", retrying.";
                    SetCommitState(DOWNLOAD_TEMPORARY_ERROR);
                    RetryWithBackoff([this] { StartDownload(); });
                    return;
                  }
                  if (!commit_pack) {
                    FXL_LOG(ERROR) << "Null commits despite status OK.";
                    SetCommitState(DOWNLOAD_PERMANENT_ERROR);
                    return;
                  }
                  backoff_->Reset();

                  cloud_provider::Commits commits_container;
                  if (!ledger::DecodeFromBuffer(commit_pack->buffer, &commits_container)) {
                    FXL_LOG(ERROR) << "Failed to decode the commits.";
                    SetCommitState(DOWNLOAD_PERMANENT_ERROR);
                    return;
                  }

                  std::vector<cloud_provider::Commit> commits =
                      std::move(commits_container.commits);
                  if (commits.empty()) {
                    // If there is no remote commits to add, announce that
                    // we're done.
                    FXL_VLOG(1) << log_prefix_ << "initial sync finished, no new remote commits";
                    BacklogDownloaded();
                  } else {
                    FXL_VLOG(1) << log_prefix_ << "retrieved " << commits.size()
                                << " (possibly) new remote commits, "
                                << "adding them to storage.";
                    // If not, fire the backlog download callback when the
                    // remote commits are downloaded.
                    const auto commit_count = commits.size();
                    DownloadBatch(std::move(commits), std::move(position_token),
                                  [this, commit_count] {
                                    FXL_VLOG(1) << log_prefix_ << "initial sync finished, added "
                                                << commit_count << " remote commits.";
                                    BacklogDownloaded();
                                  });
                  }
                });
      }));
}

bool PageDownload::IsPaused() {
  return IsIdle() || GetMergedState(commit_state_, current_get_calls_) == DOWNLOAD_TEMPORARY_ERROR;
}

bool PageDownload::IsIdle() {
  switch (GetMergedState(commit_state_, current_get_calls_)) {
    case DOWNLOAD_NOT_STARTED:
    case DOWNLOAD_IDLE:
    case DOWNLOAD_PERMANENT_ERROR:
      return true;
      break;
    case DOWNLOAD_BACKLOG:
    case DOWNLOAD_TEMPORARY_ERROR:
    case DOWNLOAD_SETTING_REMOTE_WATCHER:
    case DOWNLOAD_IN_PROGRESS:
      return false;
      break;
  }
}

void PageDownload::BacklogDownloaded() { SetRemoteWatcher(false); }

void PageDownload::SetRemoteWatcher(bool is_retry) {
  FXL_DCHECK(commit_state_ == DOWNLOAD_BACKLOG || commit_state_ == DOWNLOAD_TEMPORARY_ERROR)
      << "Current state: " << commit_state_;
  SetCommitState(DOWNLOAD_SETTING_REMOTE_WATCHER);
  // Retrieve the server-side token of the last commit we received.
  std::string last_commit_token_id;
  storage_->GetSyncMetadata(
      kTimestampKey, task_runner_->MakeScoped([this, is_retry](ledger::Status status,
                                                               std::string last_commit_token_id) {
        if (status != ledger::Status::OK && status != ledger::Status::INTERNAL_NOT_FOUND) {
          HandleDownloadCommitError("Failed to retrieve the sync metadata.");
          return;
        }

        std::unique_ptr<cloud_provider::PositionToken> position_token;
        if (!last_commit_token_id.empty()) {
          position_token = std::make_unique<cloud_provider::PositionToken>();
          position_token->opaque_id = convert::ToArray(last_commit_token_id);
        }
        cloud_provider::PageCloudWatcherPtr watcher;
        watcher_binding_.Bind(watcher.NewRequest());
        (*page_cloud_)
            ->SetWatcher(std::move(position_token), std::move(watcher), [this](auto status) {
              // This should always succeed - any errors are reported
              // through OnError().
              if (status != cloud_provider::Status::OK) {
                HandleDownloadCommitError("Unexpected error when setting the PageCloudWatcher.");
              }
            });
        SetCommitState(DOWNLOAD_IDLE);
        if (is_retry) {
          FXL_LOG(INFO) << log_prefix_ << "Cloud watcher re-established";
        }
      }));
}

void PageDownload::OnNewCommits(cloud_provider::CommitPack commit_pack,
                                cloud_provider::PositionToken position_token,
                                OnNewCommitsCallback callback) {
  cloud_provider::Commits commits_container;
  if (!ledger::DecodeFromBuffer(commit_pack.buffer, &commits_container)) {
    HandleDownloadCommitError("Failed to decode the commits");
    return;
  }

  std::vector<cloud_provider::Commit> commits = std::move(commits_container.commits);

  if (batch_download_) {
    // If there is already a commit batch being downloaded, save the new commits
    // to be downloaded when it is done.
    std::move(commits.begin(), commits.end(), std::back_inserter(commits_to_download_));
    position_token_ = fidl::MakeOptional(std::move(position_token));
    callback();
    return;
  }
  SetCommitState(DOWNLOAD_IN_PROGRESS);
  DownloadBatch(std::move(commits), fidl::MakeOptional(std::move(position_token)),
                std::move(callback));
}

void PageDownload::OnNewObject(std::vector<uint8_t> /*id*/, fuchsia::mem::Buffer /*data*/,
                               OnNewObjectCallback /*callback*/) {
  // No known cloud provider implementations use this method.
  // TODO(ppi): implement this method when we have such cloud provider
  // implementations.
  FXL_NOTIMPLEMENTED();
}

void PageDownload::OnError(cloud_provider::Status status) {
  FXL_DCHECK(commit_state_ == DOWNLOAD_IDLE || commit_state_ == DOWNLOAD_IN_PROGRESS);
  if (!IsPermanentError(status)) {
    // Reset the watcher and schedule a retry.
    if (watcher_binding_.is_bound()) {
      watcher_binding_.Unbind();
    }
    SetCommitState(DOWNLOAD_TEMPORARY_ERROR);
    FXL_LOG(WARNING) << log_prefix_ << "Connection error in the remote commit watcher, retrying.";
    RetryWithBackoff([this] { SetRemoteWatcher(true); });
    return;
  }

  if (status == cloud_provider::Status::PARSE_ERROR) {
    HandleDownloadCommitError("Received a malformed remote commit notification.");
    return;
  }

  FXL_LOG(WARNING) << "Received unexpected error from PageCloudWatcher: "
                   << fidl::ToUnderlying(status);
  HandleDownloadCommitError("Received unexpected error from PageCloudWatcher.");
}

void PageDownload::DownloadBatch(std::vector<cloud_provider::Commit> commits,
                                 std::unique_ptr<cloud_provider::PositionToken> position_token,
                                 fit::closure on_done) {
  FXL_DCHECK(!batch_download_);
  batch_download_ = std::make_unique<BatchDownload>(
      storage_, encryption_service_, std::move(commits), std::move(position_token),
      [this, on_done = std::move(on_done)] {
        if (on_done) {
          on_done();
        }
        batch_download_.reset();

        if (commits_to_download_.empty()) {
          // Don't set to idle if we're in process of setting the remote
          // watcher.
          if (commit_state_ == DOWNLOAD_IN_PROGRESS) {
            SetCommitState(DOWNLOAD_IDLE);
          }
          return;
        }
        auto commits = std::move(commits_to_download_);
        commits_to_download_.clear();
        DownloadBatch(std::move(commits), std::move(position_token_), nullptr);
      },
      [this] { HandleDownloadCommitError("Failed to persist a remote commit in storage"); });
  batch_download_->Start();
}

void PageDownload::GetObject(
    storage::ObjectIdentifier object_identifier,
    storage::RetrievedObjectType /*retrieved_object_type*/,
    fit::function<void(ledger::Status, storage::ChangeSource, storage::IsObjectSynced,
                       std::unique_ptr<storage::DataSource::DataChunk>)>
        callback) {
  GetObject(std::move(object_identifier), std::move(callback));
}

void PageDownload::GetObject(
    storage::ObjectIdentifier object_identifier,
    fit::function<void(ledger::Status, storage::ChangeSource, storage::IsObjectSynced,
                       std::unique_ptr<storage::DataSource::DataChunk>)>
        callback) {
  current_get_calls_++;
  UpdateDownloadState();
  encryption_service_->GetObjectName(
      object_identifier,
      task_runner_->MakeScoped([this, object_identifier, callback = std::move(callback)](
                                   encryption::Status status, std::string object_name) mutable {
        if (status != encryption::Status::OK) {
          HandleGetObjectError(std::move(object_identifier), encryption::IsPermanentError(status),
                               "encryption", std::move(callback));
          return;
        }
        (*page_cloud_)
            ->GetObject(convert::ToArray(object_name),
                        [this, object_identifier = std::move(object_identifier),
                         callback = std::move(callback)](cloud_provider::Status status,
                                                         ::fuchsia::mem::BufferPtr data) mutable {
                          if (status != cloud_provider::Status::OK) {
                            HandleGetObjectError(std::move(object_identifier),
                                                 IsPermanentError(status), "cloud provider",
                                                 std::move(callback));
                            return;
                          }
                          fsl::SizedVmo sized_vmo;
                          if (!fsl::SizedVmo::FromTransport(std::move(*data), &sized_vmo)) {
                            HandleGetObjectError(std::move(object_identifier), true,
                                                 "converting to SizedVmo", std::move(callback));
                            return;
                          }

                          DecryptObject(std::move(object_identifier),
                                        storage::DataSource::Create(std::move(sized_vmo)),
                                        std::move(callback));
                        });
      }));
}

void PageDownload::DecryptObject(
    storage::ObjectIdentifier object_identifier, std::unique_ptr<storage::DataSource> content,
    fit::function<void(ledger::Status, storage::ChangeSource, storage::IsObjectSynced,
                       std::unique_ptr<storage::DataSource::DataChunk>)>
        callback) {
  storage::ReadDataSource(
      &managed_container_, std::move(content),
      [this, object_identifier = std::move(object_identifier), callback = std::move(callback)](
          ledger::Status status, std::unique_ptr<storage::DataSource::DataChunk> content) mutable {
        if (status != ledger::Status::OK) {
          HandleGetObjectError(std::move(object_identifier), true, "io", std::move(callback));
          return;
        }
        encryption_service_->DecryptObject(
            object_identifier, content->Get().ToString(),
            [this, object_identifier, callback = std::move(callback)](encryption::Status status,
                                                                      std::string content) mutable {
              if (status != encryption::Status::OK) {
                HandleGetObjectError(object_identifier, encryption::IsPermanentError(status),
                                     "encryption", std::move(callback));
                return;
              }
              backoff_->Reset();
              callback(ledger::Status::OK, storage::ChangeSource::CLOUD,
                       storage::IsObjectSynced::YES,
                       storage::DataSource::DataChunk::Create(std::move(content)));
              current_get_calls_--;
              UpdateDownloadState();
            });
      });
}

void PageDownload::ReadDiffEntry(
    const cloud_provider::DiffEntry& change,
    fit::function<void(ledger::Status, storage::EntryChange)> callback) {
  storage::EntryChange result;
  if (!change.has_entry_id() || change.entry_id().empty() || !change.has_operation() ||
      !change.has_data()) {
    callback(ledger::Status::INVALID_ARGUMENT, {});
    return;
  }

  result.deleted = change.operation() == cloud_provider::Operation::DELETION;

  encryption_service_->DecryptEntryPayload(
      convert::ToString(change.data()),
      callback::MakeScoped(
          weak_factory_.GetWeakPtr(),
          [this, entry_id = change.entry_id(), result = std::move(result),
           callback = std::move(callback)](encryption::Status status,
                                           std::string decrypted_entry_payload) mutable {
            if (status != encryption::Status::OK) {
              callback(ledger::Status::INVALID_ARGUMENT, std::move(result));
              return;
            }
            if (!DecodeEntryPayload(std::move(entry_id), std::move(decrypted_entry_payload),
                                    storage_->GetObjectIdentifierFactory(), &result.entry)) {
              callback(ledger::Status::INVALID_ARGUMENT, std::move(result));
              return;
            }
            callback(ledger::Status::OK, std::move(result));
          }));
}

void PageDownload::DecodeAndParseDiff(
    const cloud_provider::DiffPack& diff_pack,
    fit::function<void(ledger::Status, storage::CommitId, std::vector<storage::EntryChange>)>
        callback) {
  cloud_provider::Diff diff;
  std::optional<std::string> base_remote_commit_id;
  std::vector<storage::EntryChange> changes;
  if (!ledger::DecodeFromBuffer(diff_pack.buffer, &diff)) {
    callback(ledger::Status::INVALID_ARGUMENT, {}, {});
    return;
  }

  if (!diff.has_base_state()) {
    callback(ledger::Status::INVALID_ARGUMENT, {}, {});
    return;
  }
  if (diff.base_state().is_empty_page()) {
    base_remote_commit_id = std::nullopt;
  } else if (diff.base_state().is_at_commit()) {
    base_remote_commit_id = convert::ToString(diff.base_state().at_commit());
  } else {
    callback(ledger::Status::INVALID_ARGUMENT, {}, {});
    return;
  }

  if (!diff.has_changes()) {
    callback(ledger::Status::INVALID_ARGUMENT, {}, {});
    return;
  }

  auto waiter = fxl::MakeRefCounted<callback::Waiter<ledger::Status, storage::EntryChange>>(
      ledger::Status::OK);
  for (const cloud_provider::DiffEntry& cloud_change : diff.changes()) {
    ReadDiffEntry(cloud_change, waiter->NewCallback());
  }

  waiter->Finalize(task_runner_->MakeScoped(
      [this, base_remote_commit_id = std::move(base_remote_commit_id),
       callback = std::move(callback)](ledger::Status status,
                                       std::vector<storage::EntryChange> changes) mutable {
        if (!base_remote_commit_id) {
          callback(status, storage::kFirstPageCommitId.ToString(), std::move(changes));
        } else {
          storage_->GetCommitIdFromRemoteId(
              *base_remote_commit_id,
              [callback = std::move(callback), changes = std::move(changes)](
                  ledger::Status status, storage::CommitId base_commit_id) mutable {
                callback(status, std::move(base_commit_id), std::move(changes));
              });
        }
      }));
}

void PageDownload::GetDiff(
    storage::CommitId commit_id, std::vector<storage::CommitId> possible_bases,
    fit::function<void(ledger::Status, storage::CommitId, std::vector<storage::EntryChange>)>
        callback) {
  current_get_calls_++;
  UpdateDownloadState();

  std::string remote_commit_id = encryption_service_->EncodeCommitId(commit_id);
  std::vector<std::vector<uint8_t>> bases_as_bytes;
  bases_as_bytes.reserve(possible_bases.size());
  for (auto& base : possible_bases) {
    bases_as_bytes.push_back(convert::ToArray(encryption_service_->EncodeCommitId(base)));
  }

  (*page_cloud_)
      ->GetDiff(
          convert::ToArray(remote_commit_id), std::move(bases_as_bytes),
          [this, callback = std::move(callback), commit_id = std::move(commit_id),
           possible_bases = std::move(possible_bases)](
              cloud_provider::Status status,
              std::unique_ptr<cloud_provider::DiffPack> diff_pack) mutable {
            if (status == cloud_provider::Status::NOT_SUPPORTED) {
              // The cloud provider does not support diff. Ask the storage to apply
              // an empty diff to the root of the same commit.
              // TODO(12356): remove compatibility.
              callback(ledger::Status::OK, std::move(commit_id), {});
              current_get_calls_--;
              UpdateDownloadState();
              return;
            }

            if (status != cloud_provider::Status::OK) {
              HandleGetDiffError(std::move(commit_id), std::move(possible_bases),
                                 IsPermanentError(status), "cloud provider", std::move(callback));
              return;
            }

            if (!diff_pack) {
              HandleGetDiffError(std::move(commit_id), std::move(possible_bases),
                                 /*is_permanent*/ true, "missing diff", std::move(callback));
              return;
            }

            DecodeAndParseDiff(
                *diff_pack,
                callback::MakeScoped(
                    weak_factory_.GetWeakPtr(),
                    [this, commit_id, possible_bases = std::move(possible_bases),
                     callback = std::move(callback)](
                        ledger::Status status, storage::CommitId base_commit,
                        std::vector<storage::EntryChange> changes) mutable {
                      if (status != ledger::Status::OK) {
                        HandleGetDiffError(std::move(commit_id), std::move(possible_bases),
                                           /*is_permanent*/ true, "invalid diff during decoding",
                                           std::move(callback));
                        return;
                      }
                      if (!NormalizeDiff(&changes)) {
                        HandleGetDiffError(std::move(commit_id), std::move(possible_bases),
                                           /*is_permanent*/ true,
                                           "invalid diff during normalization",
                                           std::move(callback));
                        return;
                      }
                      callback(ledger::Status::OK, std::move(base_commit), std::move(changes));
                      current_get_calls_--;
                      UpdateDownloadState();
                    }));
          });
}

void PageDownload::HandleGetObjectError(
    storage::ObjectIdentifier object_identifier, bool is_permanent, fxl::StringView error_name,
    fit::function<void(ledger::Status, storage::ChangeSource, storage::IsObjectSynced,
                       std::unique_ptr<storage::DataSource::DataChunk>)>
        callback) {
  if (is_permanent) {
    backoff_->Reset();
    FXL_LOG(WARNING) << log_prefix_ << "GetObject() failed due to a permanent " << error_name
                     << " error.";
    callback(ledger::Status::IO_ERROR, storage::ChangeSource::CLOUD, storage::IsObjectSynced::YES,
             nullptr);
    current_get_calls_--;
    UpdateDownloadState();
    return;
  }
  FXL_LOG(WARNING) << log_prefix_ << "GetObject() failed due to a " << error_name
                   << " error, retrying.";
  current_get_calls_--;
  UpdateDownloadState();
  RetryWithBackoff([this, object_identifier = std::move(object_identifier),
                    callback = std::move(callback)]() mutable {
    GetObject(object_identifier, std::move(callback));
  });
}

void PageDownload::HandleGetDiffError(
    storage::CommitId commit_id, std::vector<storage::CommitId> possible_bases, bool is_permanent,
    fxl::StringView error_name,
    fit::function<void(ledger::Status, storage::CommitId, std::vector<storage::EntryChange>)>
        callback) {
  if (is_permanent) {
    backoff_->Reset();
    FXL_LOG(WARNING) << log_prefix_ << "GetDiff() failed due to a permanent " << error_name
                     << " error.";
    callback(ledger::Status::IO_ERROR, "", {});
    current_get_calls_--;
    UpdateDownloadState();
    return;
  }
  FXL_LOG(WARNING) << log_prefix_ << "GetDiff() failed due to a " << error_name
                   << " error, retrying.";
  current_get_calls_--;
  UpdateDownloadState();
  RetryWithBackoff([this, commit_id = std::move(commit_id),
                    possible_bases = std::move(possible_bases),
                    callback = std::move(callback)]() mutable {
    GetDiff(std::move(commit_id), std::move(possible_bases), std::move(callback));
  });
}

void PageDownload::HandleDownloadCommitError(fxl::StringView error_description) {
  FXL_LOG(ERROR) << log_prefix_ << error_description << " Stopping sync.";
  if (watcher_binding_.is_bound()) {
    watcher_binding_.Unbind();
  }
  SetCommitState(DOWNLOAD_PERMANENT_ERROR);
}

void PageDownload::SetCommitState(DownloadSyncState new_state) {
  if (new_state == commit_state_) {
    return;
  }

  commit_state_ = new_state;
  UpdateDownloadState();
}

void PageDownload::UpdateDownloadState() {
  DownloadSyncState new_state = GetMergedState(commit_state_, current_get_calls_);

  // Notify only if the externally visible state changed.
  if (new_state != merged_state_) {
    merged_state_ = new_state;
    delegate_->SetDownloadState(GetMergedState(commit_state_, current_get_calls_));
  }
}

void PageDownload::RetryWithBackoff(fit::closure callable) {
  task_runner_->PostDelayedTask(
      [this, callable = std::move(callable)]() {
        if (this->commit_state_ != DOWNLOAD_PERMANENT_ERROR) {
          callable();
        }
      },
      backoff_->GetNext());
}

}  // namespace cloud_sync
