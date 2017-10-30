// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/test/test_page_cloud.h"

#include "lib/fsl/socket/strings.h"
#include "lib/fsl/vmo/strings.h"
#include "peridot/bin/ledger/convert/convert.h"

namespace cloud_sync {
namespace test {

cloud_provider::CommitPtr MakeCommit(
    encryption::FakeEncryptionService* encryption_service,
    const std::string& id,
    const std::string& data) {
  auto commit = cloud_provider::Commit::New();
  commit->id = convert::ToArray(id);
  commit->data =
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
void TestPageCloud::AddCommits(fidl::Array<cloud_provider::CommitPtr> commits,
                               const AddCommitsCallback& callback) {
  add_commits_calls++;
  for (auto& commit : commits) {
    ReceivedCommit received_commit;
    received_commit.id = convert::ToString(commit->id);
    received_commit.data = convert::ToString(commit->data);
    received_commits.push_back(std::move(received_commit));
  }
  callback(commit_status_to_return);
}

void TestPageCloud::GetCommits(fidl::Array<uint8_t> /*min_position_token*/,
                               const GetCommitsCallback& callback) {
  get_commits_calls++;
  callback(status_to_return, std::move(commits_to_return),
           std::move(position_token_to_return));
}

void TestPageCloud::AddObject(fidl::Array<uint8_t> id,
                              zx::vmo data,
                              const AddObjectCallback& callback) {
  add_object_calls++;
  std::string received_data;
  if (!fsl::StringFromVmo(data, &received_data)) {
    callback(cloud_provider::Status::INTERNAL_ERROR);
    return;
  }
  received_objects[convert::ToString(id)] = received_data;
  fxl::Closure report_result = [callback, status = object_status_to_return] {
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

void TestPageCloud::GetObject(fidl::Array<uint8_t> id,
                              const GetObjectCallback& callback) {
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
    fidl::Array<uint8_t> min_position_token,
    fidl::InterfaceHandle<cloud_provider::PageCloudWatcher> watcher,
    const SetWatcherCallback& callback) {
  set_watcher_position_tokens.push_back(convert::ToString(min_position_token));
  set_watcher = cloud_provider::PageCloudWatcherPtr::Create(std::move(watcher));
  callback(status_to_return);
}

}  // namespace test
}  // namespace cloud_sync
