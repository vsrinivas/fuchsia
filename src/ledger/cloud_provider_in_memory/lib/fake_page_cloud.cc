// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/cloud_provider_in_memory/lib/fake_page_cloud.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>

#include <functional>

#include "peridot/lib/rng/random.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/encoding/encoding.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/socket/strings.h"
#include "src/ledger/lib/vmo/strings.h"
#include "third_party/murmurhash/murmurhash.h"

namespace ledger {

namespace {

// Number of errors to inject before allowing a request to succeed when
// configured to inject network errors.
constexpr size_t kInitialRemainingErrorsToInject = 2;

// Seed for the murmur hash algorithm to ensure different request signatures.
constexpr uint32_t kAddCommitsSeed = 1u;
constexpr uint32_t kGetCommitsSeed = 2u;
constexpr uint32_t kAddObjectSeed = 3u;
constexpr uint32_t kGetObjectSeed = 4u;

cloud_provider::PositionToken PositionToToken(size_t position) {
  std::string bytes(std::string(reinterpret_cast<char*>(&position), sizeof(position)));
  cloud_provider::PositionToken result;
  result.opaque_id = convert::ToArray(bytes);
  return result;
}

bool TokenToPosition(const std::unique_ptr<cloud_provider::PositionToken>& token, size_t* result) {
  if (!token) {
    *result = 0u;
    return true;
  }

  if (token->opaque_id.size() != sizeof(*result)) {
    return false;
  }

  memcpy(result, token->opaque_id.data(), sizeof(*result));
  return true;
}

uint64_t GetVectorSignature(const std::vector<uint8_t>& vector, uint32_t seed) {
  return murmurhash(reinterpret_cast<const char*>(vector.data()), vector.size(), seed);
}

uint64_t GetCommitsSignature(const std::vector<cloud_provider::Commit>& commits) {
  uint64_t result = 0;
  for (const auto& commit : commits) {
    // Ignore invalid commits.
    if (commit.has_id()) {
      result = result ^ GetVectorSignature(convert::ToArray(commit.id()), kAddCommitsSeed);
    }
  }
  return result;
}

cloud_provider::Commit MakeFidlCommit(CommitRecord commit) {
  cloud_provider::Commit result;
  result.set_id(convert::ToArray(commit.id));
  result.set_data(convert::ToArray(commit.data));
  return result;
}

bool DecodeDiffEntry(const cloud_provider::DiffEntry& entry, CloudDiffEntry* result) {
  if (!entry.has_entry_id() || !entry.has_operation() || !entry.has_data()) {
    return false;
  }

  result->entry_id = convert::ToString(entry.entry_id());
  result->operation = entry.operation();
  result->data = convert::ToString(entry.data());
  return true;
}

bool DecodeDiff(const cloud_provider::Diff& diff, PageState* page_state,
                std::vector<CloudDiffEntry>* diff_entries) {
  if (!diff.has_base_state() || !diff.has_changes()) {
    return false;
  }

  if (diff.base_state().is_empty_page()) {
    *page_state = std::nullopt;
  } else if (diff.base_state().is_at_commit()) {
    *page_state = convert::ToString(diff.base_state().at_commit());
  } else {
    return false;
  }

  diff_entries->clear();
  diff_entries->reserve(diff.changes().size());
  for (auto& diff_entry : diff.changes()) {
    CloudDiffEntry decoded;
    if (!DecodeDiffEntry(diff_entry, &decoded)) {
      return false;
    }
    diff_entries->push_back(std::move(decoded));
  }

  return true;
}

}  // namespace

class FakePageCloud::WatcherContainer {
 public:
  WatcherContainer(cloud_provider::PageCloudWatcherPtr watcher, size_t next_commit_index);
  WatcherContainer(const WatcherContainer&) = delete;
  WatcherContainer& operator=(const WatcherContainer&) = delete;

  void SendCommits(std::vector<cloud_provider::Commit> commits, size_t next_commit_index,
                   fit::closure on_ack);

  size_t NextCommitIndex() { return next_commit_index_; }

  bool WaitingForWatcherAck() { return waiting_for_watcher_ack_; }

  void SetOnDiscardable(fit::closure on_discardable) {
    watcher_.set_error_handler(
        [this, on_discardable = std::move(on_discardable)](zx_status_t status) {
          watcher_.Unbind();
          on_discardable();
        });
  }

  bool IsDiscardable() const { return !watcher_.is_bound(); }

 private:
  cloud_provider::PageCloudWatcherPtr watcher_;
  // Whether we're still waiting for the watcher to ack the previous commit
  // notification.
  bool waiting_for_watcher_ack_ = false;

  // Index of the first commit to be sent to the watcher.
  size_t next_commit_index_ = 0;
};

FakePageCloud::WatcherContainer::WatcherContainer(cloud_provider::PageCloudWatcherPtr watcher,
                                                  size_t next_commit_index)
    : watcher_(std::move(watcher)), next_commit_index_(next_commit_index) {}

void FakePageCloud::WatcherContainer::SendCommits(std::vector<cloud_provider::Commit> commits,
                                                  size_t next_commit_index, fit::closure on_ack) {
  LEDGER_DCHECK(watcher_.is_bound());
  LEDGER_DCHECK(!waiting_for_watcher_ack_);
  LEDGER_DCHECK(!commits.empty());

  waiting_for_watcher_ack_ = true;
  next_commit_index_ = next_commit_index;
  cloud_provider::CommitPack commit_pack;
  if (!EncodeToBuffer(&commits, &commit_pack.buffer)) {
    watcher_->OnError(cloud_provider::Status::INTERNAL_ERROR);
    return;
  }
  watcher_->OnNewCommits(std::move(commit_pack), PositionToToken(next_commit_index),
                         [this, on_ack = std::move(on_ack)] {
                           waiting_for_watcher_ack_ = false;
                           on_ack();
                         });
}

FakePageCloud::FakePageCloud(async_dispatcher_t* dispatcher, rng::Random* random,
                             InjectNetworkError inject_network_error,
                             InjectMissingDiff inject_missing_diff)
    : random_(random),
      inject_network_error_(inject_network_error),
      inject_missing_diff_(inject_missing_diff),
      containers_(dispatcher) {
  bindings_.set_empty_set_handler([this] {
    if (on_discardable_) {
      on_discardable_();
    }
  });
}

FakePageCloud::~FakePageCloud() = default;

bool FakePageCloud::IsDiscardable() const { return bindings_.size() == 0; }

void FakePageCloud::SetOnDiscardable(fit::closure on_discardable) {
  on_discardable_ = std::move(on_discardable);
}

void FakePageCloud::Bind(fidl::InterfaceRequest<cloud_provider::PageCloud> request) {
  bindings_.AddBinding(this, std::move(request));
}

void FakePageCloud::SendPendingCommits() {
  for (auto& container : containers_) {
    if (container.WaitingForWatcherAck() || container.NextCommitIndex() >= commits_.size()) {
      continue;
    }

    std::vector<cloud_provider::Commit> commits;
    for (size_t i = container.NextCommitIndex(); i < commits_.size(); i++) {
      commits.push_back(MakeFidlCommit(commits_[i]));
    }

    container.SendCommits(std::move(commits), commits_.size(), [this] { SendPendingCommits(); });
  }
}

bool FakePageCloud::MustReturnError(uint64_t request_signature) {
  switch (inject_network_error_) {
    case InjectNetworkError::NO:
      return false;
    case InjectNetworkError::YES:
      auto it = remaining_errors_to_inject_.find(request_signature);
      if (it == remaining_errors_to_inject_.end()) {
        remaining_errors_to_inject_[request_signature] = kInitialRemainingErrorsToInject;
        it = remaining_errors_to_inject_.find(request_signature);
      }
      if (it->second) {
        it->second--;
        return true;
      }
      remaining_errors_to_inject_.erase(it);
      return false;
  }
}

void FakePageCloud::AddCommits(cloud_provider::CommitPack commit_pack,
                               AddCommitsCallback callback) {
  cloud_provider::Commits commits;
  if (!DecodeFromBuffer(commit_pack.buffer, &commits)) {
    callback(cloud_provider::Status::INTERNAL_ERROR);
    return;
  }
  std::vector<cloud_provider::Commit> commit_entries = std::move(commits.commits);
  if (MustReturnError(GetCommitsSignature(commit_entries))) {
    callback(cloud_provider::Status::NETWORK_ERROR);
    return;
  }

  // Check that the commits are valid to insert: the base of the diff must be uploaded before the
  // diff to avoid cycles.
  std::set<std::string> commits_in_batch;
  std::vector<std::pair<std::string, std::string>> commits_to_insert;
  std::vector<std::tuple<std::string, PageState, std::vector<CloudDiffEntry>>> diffs_to_insert;
  for (const cloud_provider::Commit& commit : commit_entries) {
    if (!commit.has_id() || !commit.has_data()) {
      callback(cloud_provider::Status::ARGUMENT_ERROR);
      return;
    }
    std::string commit_id = convert::ToString(commit.id());
    if (known_commits_.find(commit_id) != known_commits_.end()) {
      // The commit already exists, we will not insert it again.
      continue;
    }
    if (commits_in_batch.find(commit_id) != commits_in_batch.end()) {
      // The commit is present twice in the pack.
      callback(cloud_provider::Status::ARGUMENT_ERROR);
      return;
    }
    if (commit.has_diff()) {
      PageState diff_base;
      std::vector<CloudDiffEntry> diff_entries;
      if (!DecodeDiff(commit.diff(), &diff_base, &diff_entries)) {
        callback(cloud_provider::Status::ARGUMENT_ERROR);
        return;
      }
      if (diff_base && known_commits_.find(*diff_base) == known_commits_.end() &&
          commits_in_batch.find(*diff_base) == commits_in_batch.end()) {
        // The diff parent commit is unknown, reject the diff.
        callback(cloud_provider::Status::NOT_FOUND);
        return;
      }
      diffs_to_insert.emplace_back(commit_id, std::move(diff_base), std::move(diff_entries));
    }
    commits_in_batch.insert(commit_id);
    commits_to_insert.emplace_back(std::move(commit_id), convert::ToString(commit.data()));
  }

  // The commits are valid, we can insert them.
  for (auto& [commit_id, commit_data] : commits_to_insert) {
    known_commits_.insert(commit_id);
    commits_.push_back({std::move(commit_id), std::move(commit_data)});
  }
  for (auto& [commit_id, diff_base, diff_entries] : diffs_to_insert) {
    // Randomly ignore some diffs if missing diff injection is enabled.
    if (inject_missing_diff_ == InjectMissingDiff::YES && random_->Draw<uint8_t>() % 2 == 0) {
      continue;
    }
    diffs_.AddDiff(std::move(commit_id), std::move(diff_base), std::move(diff_entries));
  }

  SendPendingCommits();
  callback(cloud_provider::Status::OK);
}

void FakePageCloud::GetCommits(std::unique_ptr<cloud_provider::PositionToken> min_position_token,
                               GetCommitsCallback callback) {
  if (MustReturnError(GetVectorSignature(
          min_position_token ? min_position_token->opaque_id : std::vector<uint8_t>(),
          kGetCommitsSeed))) {
    callback(cloud_provider::Status::NETWORK_ERROR, nullptr, nullptr);
    return;
  }
  std::vector<cloud_provider::Commit> result;
  size_t start = 0u;
  if (!TokenToPosition(min_position_token, &start)) {
    callback(cloud_provider::Status::ARGUMENT_ERROR, nullptr, nullptr);
    return;
  }

  for (size_t i = start; i < commits_.size(); i++) {
    result.push_back(MakeFidlCommit(commits_[i]));
  }
  std::unique_ptr<cloud_provider::PositionToken> token;
  if (!result.empty()) {
    // This will cause the last commit to be delivered again when the token is
    // used for the next GetCommits() call. This is allowed by the FIDL contract
    // and should be handled correctly by the client.
    token = fidl::MakeOptional(PositionToToken(commits_.size() - 1));
  }
  cloud_provider::CommitPack commit_pack;
  if (!EncodeToBuffer(&result, &commit_pack.buffer)) {
    callback(cloud_provider::Status::INTERNAL_ERROR, nullptr, nullptr);
    return;
  }
  callback(cloud_provider::Status::OK, fidl::MakeOptional(std::move(commit_pack)),
           std::move(token));
}

void FakePageCloud::AddObject(std::vector<uint8_t> id, fuchsia::mem::Buffer data,
                              cloud_provider::ReferencePack /*references*/,
                              AddObjectCallback callback) {
  if (MustReturnError(GetVectorSignature(id, kAddObjectSeed))) {
    callback(cloud_provider::Status::NETWORK_ERROR);
    return;
  }
  std::string bytes;
  if (!StringFromVmo(data, &bytes)) {
    callback(cloud_provider::Status::INTERNAL_ERROR);
    return;
  }

  objects_[convert::ToString(id)] = bytes;
  callback(cloud_provider::Status::OK);
}

void FakePageCloud::GetObject(std::vector<uint8_t> id, GetObjectCallback callback) {
  if (MustReturnError(GetVectorSignature(id, kGetObjectSeed))) {
    callback(cloud_provider::Status::NETWORK_ERROR, nullptr);
    return;
  }
  std::string id_str = convert::ToString(id);
  if (!objects_.count(id_str)) {
    callback(cloud_provider::Status::NOT_FOUND, nullptr);
    return;
  }
  ::fuchsia::mem::Buffer buffer;
  if (!VmoFromString(objects_[id_str], &buffer)) {
    callback(cloud_provider::Status::INTERNAL_ERROR, nullptr);
    return;
  }
  callback(cloud_provider::Status::OK, fidl::MakeOptional(std::move(buffer)));
}

void FakePageCloud::SetWatcher(std::unique_ptr<cloud_provider::PositionToken> min_position_token,
                               fidl::InterfaceHandle<cloud_provider::PageCloudWatcher> watcher,
                               SetWatcherCallback callback) {
  // TODO(qsr): Inject errors here when LE-438 is fixed.
  // TODO(ppi): for the cloud provider to be useful for Voila, we need
  // to support multiple watchers.
  auto watcher_ptr = watcher.Bind();

  size_t first_pending_commit_index;
  if (!TokenToPosition(min_position_token, &first_pending_commit_index)) {
    callback(cloud_provider::Status::ARGUMENT_ERROR);
    return;
  }
  containers_.emplace(std::move(watcher_ptr), first_pending_commit_index);
  SendPendingCommits();
  callback(cloud_provider::Status::OK);
}

void FakePageCloud::GetDiff(std::vector<uint8_t> commit_id,
                            std::vector<std::vector<uint8_t>> possible_bases,
                            GetDiffCallback callback) {
  // Check that the commit exists.
  std::string translated_commit_id = convert::ToString(commit_id);
  if (known_commits_.find(translated_commit_id) == known_commits_.end()) {
    callback(cloud_provider::Status::NOT_FOUND, {});
    return;
  }

  std::vector<std::string> translated_bases;
  std::transform(possible_bases.begin(), possible_bases.end(), std::back_inserter(translated_bases),
                 convert::ToString);
  auto [base_state, diff_entries] = diffs_.GetSmallestDiff(translated_commit_id, translated_bases);

  cloud_provider::Diff diff;
  if (base_state) {
    diff.mutable_base_state()->set_at_commit(convert::ToArray(*base_state));
  } else {
    diff.mutable_base_state()->set_empty_page({});
  }

  diff.set_changes({});
  for (const CloudDiffEntry& diff_entry : diff_entries) {
    cloud_provider::DiffEntry encoded;
    *encoded.mutable_entry_id() = convert::ToArray(diff_entry.entry_id);
    *encoded.mutable_operation() = diff_entry.operation;
    *encoded.mutable_data() = convert::ToArray(diff_entry.data);
    diff.mutable_changes()->push_back(std::move(encoded));
  }

  auto diff_pack = std::make_unique<cloud_provider::DiffPack>();
  if (!EncodeToBuffer(&diff, &diff_pack->buffer)) {
    callback(cloud_provider::Status::INTERNAL_ERROR, {});
    return;
  }
  callback(cloud_provider::Status::OK, std::move(diff_pack));
}

void FakePageCloud::UpdateClock(cloud_provider::ClockPack /*clock*/, UpdateClockCallback callback) {
  callback(cloud_provider::Status::NOT_SUPPORTED, nullptr);
}

}  // namespace ledger
