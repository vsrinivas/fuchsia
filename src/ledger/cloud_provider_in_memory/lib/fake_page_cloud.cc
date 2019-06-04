// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/cloud_provider_in_memory/lib/fake_page_cloud.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>
#include <lib/fsl/socket/strings.h>
#include <lib/fsl/vmo/strings.h>

#include <functional>

#include "peridot/lib/convert/convert.h"
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
  std::string bytes(
      std::string(reinterpret_cast<char*>(&position), sizeof(position)));
  cloud_provider::PositionToken result;
  result.opaque_id = convert::ToArray(bytes);
  return result;
}

bool TokenToPosition(
    const std::unique_ptr<cloud_provider::PositionToken>& token,
    size_t* result) {
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
  return murmurhash(reinterpret_cast<const char*>(vector.data()), vector.size(),
                    seed);
}

uint64_t GetCommitsSignature(
    const std::vector<cloud_provider::CommitPackEntry>& commits) {
  uint64_t result = 0;
  for (const auto& commit : commits) {
    result = result ^
             GetVectorSignature(convert::ToArray(commit.id), kAddCommitsSeed);
  }
  return result;
}

}  // namespace

class FakePageCloud::WatcherContainer {
 public:
  WatcherContainer(cloud_provider::PageCloudWatcherPtr watcher,
                   size_t next_commit_index);

  void SendCommits(std::vector<cloud_provider::CommitPackEntry> commits,
                   size_t next_commit_index, fit::closure on_ack);

  size_t NextCommitIndex() { return next_commit_index_; }

  bool WaitingForWatcherAck() { return waiting_for_watcher_ack_; }

  void set_on_empty(fit::closure on_empty) {
    watcher_.set_error_handler(
        [on_empty = std::move(on_empty)](zx_status_t status) { on_empty(); });
  }

 private:
  cloud_provider::PageCloudWatcherPtr watcher_;
  // Whether we're still waiting for the watcher to ack the previous commit
  // notification.
  bool waiting_for_watcher_ack_ = false;

  // Index of the first commit to be sent to the watcher.
  size_t next_commit_index_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(WatcherContainer);
};

FakePageCloud::WatcherContainer::WatcherContainer(
    cloud_provider::PageCloudWatcherPtr watcher, size_t next_commit_index)
    : watcher_(std::move(watcher)), next_commit_index_(next_commit_index) {}

void FakePageCloud::WatcherContainer::SendCommits(
    std::vector<cloud_provider::CommitPackEntry> commits,
    size_t next_commit_index, fit::closure on_ack) {
  FXL_DCHECK(watcher_.is_bound());
  FXL_DCHECK(!waiting_for_watcher_ack_);
  FXL_DCHECK(!commits.empty());

  waiting_for_watcher_ack_ = true;
  next_commit_index_ = next_commit_index;
  cloud_provider::CommitPack commit_pack;
  if (!cloud_provider::EncodeCommitPack(commits, &commit_pack)) {
    watcher_->OnError(cloud_provider::Status::INTERNAL_ERROR);
    return;
  }
  watcher_->OnNewCommits(std::move(commit_pack),
                         PositionToToken(next_commit_index),
                         [this, on_ack = std::move(on_ack)] {
                           waiting_for_watcher_ack_ = false;
                           on_ack();
                         });
}

FakePageCloud::FakePageCloud(InjectNetworkError inject_network_error)
    : inject_network_error_(inject_network_error) {
  bindings_.set_empty_set_handler([this] {
    if (on_empty_) {
      on_empty_();
    }
  });
}

FakePageCloud::~FakePageCloud() {}

void FakePageCloud::Bind(
    fidl::InterfaceRequest<cloud_provider::PageCloud> request) {
  bindings_.AddBinding(this, std::move(request));
}

void FakePageCloud::SendPendingCommits() {
  for (auto& container : containers_) {
    if (container.WaitingForWatcherAck() ||
        container.NextCommitIndex() >= commits_.size()) {
      continue;
    }

    std::vector<cloud_provider::CommitPackEntry> commits;
    for (size_t i = container.NextCommitIndex(); i < commits_.size(); i++) {
      commits.push_back(commits_[i]);
    }

    container.SendCommits(std::move(commits), commits_.size(),
                          [this] { SendPendingCommits(); });
  }
}

bool FakePageCloud::MustReturnError(uint64_t request_signature) {
  switch (inject_network_error_) {
    case InjectNetworkError::NO:
      return false;
    case InjectNetworkError::YES:
      auto it = remaining_errors_to_inject_.find(request_signature);
      if (it == remaining_errors_to_inject_.end()) {
        remaining_errors_to_inject_[request_signature] =
            kInitialRemainingErrorsToInject;
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

void FakePageCloud::AddCommits(cloud_provider::CommitPack commits,
                               AddCommitsCallback callback) {
  std::vector<cloud_provider::CommitPackEntry> commit_entries;
  if (!cloud_provider::DecodeCommitPack(commits, &commit_entries)) {
    callback(cloud_provider::Status::INTERNAL_ERROR);
    return;
  }

  if (MustReturnError(GetCommitsSignature(commit_entries))) {
    callback(cloud_provider::Status::NETWORK_ERROR);
    return;
  }
  std::move(commit_entries.begin(), commit_entries.end(),
            std::back_inserter(commits_));
  SendPendingCommits();
  callback(cloud_provider::Status::OK);
}

void FakePageCloud::GetCommits(
    std::unique_ptr<cloud_provider::PositionToken> min_position_token,
    GetCommitsCallback callback) {
  if (MustReturnError(GetVectorSignature(min_position_token
                                             ? min_position_token->opaque_id
                                             : std::vector<uint8_t>(),
                                         kGetCommitsSeed))) {
    callback(cloud_provider::Status::NETWORK_ERROR, nullptr, nullptr);
    return;
  }
  std::vector<cloud_provider::CommitPackEntry> result;
  size_t start = 0u;
  if (!TokenToPosition(min_position_token, &start)) {
    callback(cloud_provider::Status::ARGUMENT_ERROR, nullptr, nullptr);
    return;
  }

  for (size_t i = start; i < commits_.size(); i++) {
    result.push_back(commits_[i]);
  }
  std::unique_ptr<cloud_provider::PositionToken> token;
  if (!result.empty()) {
    // This will cause the last commit to be delivered again when the token is
    // used for the next GetCommits() call. This is allowed by the FIDL contract
    // and should be handled correctly by the client.
    token = fidl::MakeOptional(PositionToToken(commits_.size() - 1));
  }
  cloud_provider::CommitPack commit_pack;
  if (!cloud_provider::EncodeCommitPack(result, &commit_pack)) {
    callback(cloud_provider::Status::INTERNAL_ERROR, nullptr, nullptr);
    return;
  }
  callback(cloud_provider::Status::OK,
           fidl::MakeOptional(std::move(commit_pack)), std::move(token));
}

void FakePageCloud::AddObject(std::vector<uint8_t> id,
                              fuchsia::mem::Buffer data,
                              AddObjectCallback callback) {
  if (MustReturnError(GetVectorSignature(id, kAddObjectSeed))) {
    callback(cloud_provider::Status::NETWORK_ERROR);
    return;
  }
  std::string bytes;
  if (!fsl::StringFromVmo(data, &bytes)) {
    callback(cloud_provider::Status::INTERNAL_ERROR);
    return;
  }

  objects_[convert::ToString(id)] = bytes;
  callback(cloud_provider::Status::OK);
}

void FakePageCloud::GetObject(std::vector<uint8_t> id,
                              GetObjectCallback callback) {
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
  if (!fsl::VmoFromString(objects_[id_str], &buffer)) {
    callback(cloud_provider::Status::INTERNAL_ERROR, nullptr);
    return;
  }
  callback(cloud_provider::Status::OK, fidl::MakeOptional(std::move(buffer)));
}

void FakePageCloud::SetWatcher(
    std::unique_ptr<cloud_provider::PositionToken> min_position_token,
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

}  // namespace ledger
