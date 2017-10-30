// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/test/cloud_provider/fake_page_cloud.h"

#include "lib/fsl/socket/strings.h"
#include "lib/fsl/vmo/strings.h"
#include "peridot/bin/ledger/convert/convert.h"

namespace ledger {

namespace {

fidl::Array<uint8_t> PositionToToken(size_t position) {
  std::string bytes(
      std::string(reinterpret_cast<char*>(&position), sizeof(position)));
  return convert::ToArray(bytes);
}

bool TokenToPosition(const fidl::Array<uint8_t>& token, size_t* result) {
  if (token.is_null()) {
    *result = 0u;
    return true;
  }

  if (token.size() != sizeof(*result)) {
    return false;
  }

  memcpy(result, token.data(), sizeof(*result));
  return true;
}

}  // namespace

class FakePageCloud::WatcherContainer {
 public:
  WatcherContainer(cloud_provider::PageCloudWatcherPtr watcher,
                   size_t next_commit_index);

  void SendCommits(fidl::Array<cloud_provider::CommitPtr> commits,
                   size_t next_commit_index,
                   fxl::Closure on_ack);

  size_t NextCommitIndex() { return next_commit_index_; }

  bool WaitingForWatcherAck() { return waiting_for_watcher_ack_; }

  void set_on_empty(fxl::Closure on_empty) {
    watcher_.set_connection_error_handler(std::move(on_empty));
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
    cloud_provider::PageCloudWatcherPtr watcher,
    size_t next_commit_index)
    : watcher_(std::move(watcher)), next_commit_index_(next_commit_index) {}

void FakePageCloud::WatcherContainer::SendCommits(
    fidl::Array<cloud_provider::CommitPtr> commits,
    size_t next_commit_index,
    fxl::Closure on_ack) {
  FXL_DCHECK(watcher_.is_bound());
  FXL_DCHECK(!waiting_for_watcher_ack_);
  FXL_DCHECK(!commits.empty());

  waiting_for_watcher_ack_ = true;
  next_commit_index_ = next_commit_index;
  watcher_->OnNewCommits(std::move(commits), PositionToToken(next_commit_index),
                         [this, on_ack = std::move(on_ack)] {
                           waiting_for_watcher_ack_ = false;
                           on_ack();
                         });
}

FakePageCloud::FakePageCloud() {
  bindings_.set_on_empty_set_handler([this] {
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

    fidl::Array<cloud_provider::CommitPtr> commits;
    for (size_t i = container.NextCommitIndex(); i < commits_.size(); i++) {
      commits.push_back(commits_[i].Clone());
    }

    container.SendCommits(std::move(commits), commits_.size(),
                          [this] { SendPendingCommits(); });
  }
}

void FakePageCloud::AddCommits(fidl::Array<cloud_provider::CommitPtr> commits,
                               const AddCommitsCallback& callback) {
  for (auto& commit : commits) {
    commits_.push_back(std::move(commit));
  }
  SendPendingCommits();
  callback(cloud_provider::Status::OK);
}

void FakePageCloud::GetCommits(fidl::Array<uint8_t> min_position_token,
                               const GetCommitsCallback& callback) {
  fidl::Array<cloud_provider::CommitPtr> result;
  size_t start = 0u;
  if (!TokenToPosition(min_position_token, &start)) {
    callback(cloud_provider::Status::ARGUMENT_ERROR, nullptr, nullptr);
    return;
  }

  for (size_t i = start; i < commits_.size(); i++) {
    result.push_back(commits_[i].Clone());
  }
  fidl::Array<uint8_t> token;
  if (!result.empty()) {
    // This will cause the last commit to be delivered again when the token is
    // used for the next GetCommits() call. This is allowed by the FIDL contract
    // and should be handled correctly by the client.
    token = PositionToToken(commits_.size() - 1);
  }
  callback(cloud_provider::Status::OK, std::move(result), std::move(token));
}

void FakePageCloud::AddObject(fidl::Array<uint8_t> id,
                              zx::vmo data,
                              const AddObjectCallback& callback) {
  std::string bytes;
  if (!fsl::StringFromVmo(data, &bytes)) {
    callback(cloud_provider::Status::INTERNAL_ERROR);
    return;
  }

  objects_[convert::ToString(id)] = bytes;
  callback(cloud_provider::Status::OK);
}

void FakePageCloud::GetObject(fidl::Array<uint8_t> id,
                              const GetObjectCallback& callback) {
  std::string id_str = convert::ToString(id);
  if (!objects_.count(id_str)) {
    callback(cloud_provider::Status::NOT_FOUND, 0u, zx::socket());
    return;
  }

  callback(cloud_provider::Status::OK, objects_[id_str].size(),
           fsl::WriteStringToSocket(objects_[id_str]));
}

void FakePageCloud::SetWatcher(
    fidl::Array<uint8_t> min_position_token,
    fidl::InterfaceHandle<cloud_provider::PageCloudWatcher> watcher,
    const SetWatcherCallback& callback) {
  auto watcher_ptr =
      cloud_provider::PageCloudWatcherPtr::Create(std::move(watcher));

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
