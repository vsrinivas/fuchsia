// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/testing/test_page_cloud.h"

#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>

#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/fsl/socket/strings.h"
#include "src/lib/fsl/vmo/strings.h"

namespace cloud_sync {

cloud_provider::Commit MakeTestCommit(encryption::FakeEncryptionService* encryption_service,
                                      const std::string& data) {
  cloud_provider::Commit commit;
  *commit.mutable_data() = convert::ToArray(encryption_service->EncryptCommitSynchronous(data));
  *commit.mutable_id() =
      convert::ToArray(encryption_service->EncodeCommitId(storage::ComputeCommitId(data)));
  return commit;
}

std::unique_ptr<cloud_provider::CommitPack> MakeTestCommitPack(
    encryption::FakeEncryptionService* encryption_service, std::vector<std::string> commit_data) {
  cloud_provider::Commits commits_container;
  for (auto& data : commit_data) {
    commits_container.commits.push_back(MakeTestCommit(encryption_service, data));
  }
  cloud_provider::CommitPack result;
  if (!cloud_provider::EncodeToBuffer(&commits_container, &result.buffer)) {
    return nullptr;
  }
  return fidl::MakeOptional(std::move(result));
}

bool CommitHasIdAndData(const cloud_provider::Commit& commit) {
  return commit.has_id() && commit.has_data();
}

TestPageCloud::TestPageCloud(fidl::InterfaceRequest<cloud_provider::PageCloud> request)
    : binding_(this, std::move(request)) {}
TestPageCloud::~TestPageCloud() = default;

void TestPageCloud::RunPendingCallbacks() {
  for (auto& callback : pending_add_object_callbacks) {
    callback();
  }
  pending_add_object_callbacks.clear();
}

// cloud_provider::PageCloud:
void TestPageCloud::AddCommits(cloud_provider::CommitPack commit_pack,
                               AddCommitsCallback callback) {
  cloud_provider::Commits commits;
  if (!cloud_provider::DecodeFromBuffer(commit_pack.buffer, &commits)) {
    callback(cloud_provider::Status::INTERNAL_ERROR);
    return;
  }

  add_commits_calls++;
  for (auto& commit : commits.commits) {
    received_commits.push_back(std::move(commit));
  }
  callback(commit_status_to_return);
}

void TestPageCloud::GetCommits(
    std::unique_ptr<cloud_provider::PositionToken> /*min_position_token*/,
    GetCommitsCallback callback) {
  get_commits_calls++;
  cloud_provider::CommitPack commit_pack;
  cloud_provider::Commits commits_container{std::move(commits_to_return)};
  if (!cloud_provider::EncodeToBuffer(&commits_container, &commit_pack.buffer)) {
    callback(cloud_provider::Status::INTERNAL_ERROR, nullptr, nullptr);
    return;
  }
  callback(status_to_return, fidl::MakeOptional(std::move(commit_pack)),
           std::move(position_token_to_return));
}

void TestPageCloud::AddObject(std::vector<uint8_t> id, fuchsia::mem::Buffer data,
                              cloud_provider::ReferencePack /*references*/,
                              AddObjectCallback callback) {
  add_object_calls++;
  std::string received_data;
  if (!fsl::StringFromVmo(data, &received_data)) {
    callback(cloud_provider::Status::INTERNAL_ERROR);
    return;
  }
  received_objects[convert::ToString(id)] = received_data;
  fit::closure report_result = [callback = std::move(callback), status = object_status_to_return] {
    callback(status);
  };
  if (delay_add_object_callbacks) {
    pending_add_object_callbacks.push_back(std::move(report_result));
  } else {
    report_result();
  }

  if (reset_object_status_after_call) {
    object_status_to_return = cloud_provider::Status::OK;
  }
}

void TestPageCloud::GetObject(std::vector<uint8_t> id, GetObjectCallback callback) {
  get_object_calls++;
  if (status_to_return != cloud_provider::Status::OK) {
    callback(status_to_return, nullptr);
    return;
  }

  std::string object_id = convert::ToString(id);
  if (objects_to_return.count(object_id) == 0) {
    callback(cloud_provider::Status::INTERNAL_ERROR, nullptr);
    return;
  }

  ::fuchsia::mem::Buffer buffer;
  if (!fsl::VmoFromString(objects_to_return[object_id], &buffer)) {
    callback(cloud_provider::Status::INTERNAL_ERROR, nullptr);
    return;
  }
  callback(status_to_return, fidl::MakeOptional(std::move(buffer)));
}

void TestPageCloud::SetWatcher(std::unique_ptr<cloud_provider::PositionToken> min_position_token,
                               fidl::InterfaceHandle<cloud_provider::PageCloudWatcher> watcher,
                               SetWatcherCallback callback) {
  set_watcher_position_tokens.push_back(std::move(min_position_token));
  set_watcher = watcher.Bind();
  callback(status_to_return);
}

void TestPageCloud::GetDiff(std::vector<uint8_t> commit_id,
                            std::vector<std::vector<uint8_t>> possible_bases,
                            GetDiffCallback callback) {
  get_diff_calls.emplace_back(commit_id, possible_bases);
  if (status_to_return != cloud_provider::Status::OK) {
    callback(status_to_return, {});
    return;
  }

  cloud_provider::Diff diff;
  zx_status_t status = diff_to_return.Clone(&diff);
  FXL_DCHECK(status == ZX_OK);
  std::unique_ptr<cloud_provider::DiffPack> diff_pack =
      std::make_unique<cloud_provider::DiffPack>();
  bool encoded = cloud_provider::EncodeToBuffer(&diff, &diff_pack->buffer);
  FXL_DCHECK(encoded);
  callback(cloud_provider::Status::OK, std::move(diff_pack));
}

void TestPageCloud::UpdateClock(cloud_provider::ClockPack /*clock*/, UpdateClockCallback callback) {
  callback(cloud_provider::Status::NOT_SUPPORTED, nullptr);
}

}  // namespace cloud_sync
