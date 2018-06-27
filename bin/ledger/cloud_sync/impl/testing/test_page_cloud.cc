// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/testing/test_page_cloud.h"

#include <lib/fit/function.h>

#include "lib/fsl/socket/strings.h"
#include "lib/fsl/vmo/strings.h"
#include "peridot/lib/convert/convert.h"

namespace cloud_sync {

cloud_provider::Commit MakeTestCommit(
    encryption::FakeEncryptionService* encryption_service,
    const std::string& id, const std::string& data) {
  cloud_provider::Commit commit;
  commit.id = convert::ToArray(id);
  commit.data =
      convert::ToArray(encryption_service->EncryptCommitSynchronous(data));
  return commit;
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
void TestPageCloud::AddCommits(fidl::VectorPtr<cloud_provider::Commit> commits,
                               AddCommitsCallback callback) {
  add_commits_calls++;
  for (auto& commit : *commits) {
    ReceivedCommit received_commit;
    received_commit.id = convert::ToString(commit.id);
    received_commit.data = convert::ToString(commit.data);
    received_commits.push_back(std::move(received_commit));
  }
  callback(commit_status_to_return);
}

void TestPageCloud::GetCommits(
    std::unique_ptr<cloud_provider::Token> /*min_position_token*/,
    GetCommitsCallback callback) {
  get_commits_calls++;
  callback(status_to_return, std::move(commits_to_return),
           std::move(position_token_to_return));
}

void TestPageCloud::AddObject(fidl::VectorPtr<uint8_t> id,
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

void TestPageCloud::GetObject(fidl::VectorPtr<uint8_t> id,
                              GetObjectCallback callback) {
  get_object_calls++;
  if (status_to_return != cloud_provider::Status::OK) {
    callback(status_to_return, 0, zx::socket());
    return;
  }

  std::string object_id = convert::ToString(id);
  if (objects_to_return.count(object_id) == 0) {
    callback(cloud_provider::Status::INTERNAL_ERROR, 0, zx::socket());
    return;
  }

  callback(status_to_return, objects_to_return[object_id].size(),
           fsl::WriteStringToSocket(objects_to_return[object_id]));
}

void TestPageCloud::SetWatcher(
    std::unique_ptr<cloud_provider::Token> min_position_token,
    fidl::InterfaceHandle<cloud_provider::PageCloudWatcher> watcher,
    SetWatcherCallback callback) {
  set_watcher_position_tokens.push_back(std::move(min_position_token));
  set_watcher = watcher.Bind();
  callback(status_to_return);
}

}  // namespace cloud_sync
