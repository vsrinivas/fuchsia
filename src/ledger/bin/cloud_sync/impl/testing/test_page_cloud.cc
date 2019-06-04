// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/testing/test_page_cloud.h"

#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>
#include <lib/fsl/socket/strings.h>
#include <lib/fsl/vmo/strings.h>

#include "peridot/lib/convert/convert.h"

namespace cloud_sync {

cloud_provider::CommitPackEntry MakeTestCommit(
    encryption::FakeEncryptionService* encryption_service,
    const std::string& id, const std::string& data) {
  cloud_provider::CommitPackEntry commit;
  commit.id = id;
  commit.data = encryption_service->EncryptCommitSynchronous(data);
  return commit;
}

std::unique_ptr<cloud_provider::CommitPack> MakeTestCommitPack(
    encryption::FakeEncryptionService* encryption_service,
    std::vector<std::tuple<std::string, std::string>> commit_data) {
  std::vector<cloud_provider::CommitPackEntry> entries;
  for (auto& data : commit_data) {
    entries.push_back(MakeTestCommit(encryption_service, std::get<0>(data),
                                     std::get<1>(data)));
  }
  cloud_provider::CommitPack result;
  if (!cloud_provider::EncodeCommitPack(entries, &result)) {
    return nullptr;
  }
  return fidl::MakeOptional(std::move(result));
}

TestPageCloud::TestPageCloud(
    fidl::InterfaceRequest<cloud_provider::PageCloud> request)
    : binding_(this, std::move(request)) {}
TestPageCloud::~TestPageCloud() {}

void TestPageCloud::RunPendingCallbacks() {
  for (auto& callback : pending_add_object_callbacks) {
    callback();
  }
  pending_add_object_callbacks.clear();
}

// cloud_provider::PageCloud:
void TestPageCloud::AddCommits(cloud_provider::CommitPack commits,
                               AddCommitsCallback callback) {
  std::vector<cloud_provider::CommitPackEntry> entries;
  if (!cloud_provider::DecodeCommitPack(commits, &entries)) {
    callback(cloud_provider::Status::INTERNAL_ERROR);
    return;
  }

  add_commits_calls++;
  for (auto& entry : entries) {
    received_commits.push_back(std::move(entry));
  }
  callback(commit_status_to_return);
}

void TestPageCloud::GetCommits(
    std::unique_ptr<cloud_provider::PositionToken> /*min_position_token*/,
    GetCommitsCallback callback) {
  get_commits_calls++;
  cloud_provider::CommitPack commit_pack;
  if (!cloud_provider::EncodeCommitPack(commits_to_return, &commit_pack)) {
    callback(cloud_provider::Status::INTERNAL_ERROR, nullptr, nullptr);
    return;
  }
  callback(status_to_return, fidl::MakeOptional(std::move(commit_pack)),
           std::move(position_token_to_return));
}

void TestPageCloud::AddObject(std::vector<uint8_t> id,
                              fuchsia::mem::Buffer data,
                              AddObjectCallback callback) {
  add_object_calls++;
  std::string received_data;
  if (!fsl::StringFromVmo(data, &received_data)) {
    callback(cloud_provider::Status::INTERNAL_ERROR);
    return;
  }
  received_objects[convert::ToString(id)] = received_data;
  fit::closure report_result = [callback = std::move(callback),
                                status = object_status_to_return] {
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

void TestPageCloud::GetObject(std::vector<uint8_t> id,
                              GetObjectCallback callback) {
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

void TestPageCloud::SetWatcher(
    std::unique_ptr<cloud_provider::PositionToken> min_position_token,
    fidl::InterfaceHandle<cloud_provider::PageCloudWatcher> watcher,
    SetWatcherCallback callback) {
  set_watcher_position_tokens.push_back(std::move(min_position_token));
  set_watcher = watcher.Bind();
  callback(status_to_return);
}

}  // namespace cloud_sync
