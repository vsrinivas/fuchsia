// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_provider/test/test_page_cloud_handler.h"

#include "lib/fsl/socket/strings.h"
#include "lib/fxl/functional/make_copyable.h"

namespace cloud_provider_firebase {
namespace test {

TestPageCloudHandler::TestPageCloudHandler(
    fxl::RefPtr<fxl::TaskRunner> task_runner)
    : task_runner_(std::move(task_runner)) {}

TestPageCloudHandler::~TestPageCloudHandler() = default;

void TestPageCloudHandler::DeliverRemoteCommits() {
  for (auto& record : notifications_to_deliver) {
    task_runner_->PostTask(
        fxl::MakeCopyable([ this, record = std::move(record) ]() mutable {
          std::vector<Record> records;
          records.push_back(std::move(record));
          watcher->OnRemoteCommits(std::move(records));
        }));
  }
}

void TestPageCloudHandler::AddCommits(
    const std::string& /*auth_token*/,
    std::vector<Commit> commits,
    const std::function<void(Status)>& callback) {
  ++add_commits_calls;
  if (commit_status_to_return == Status::OK) {
    std::move(commits.begin(), commits.end(),
              std::back_inserter(received_commits));
  }
  task_runner_->PostTask(
      [this, callback]() { callback(commit_status_to_return); });
}

void TestPageCloudHandler::WatchCommits(const std::string& auth_token,
                                        const std::string& min_timestamp,
                                        CommitWatcher* watcher_ptr) {
  watch_commits_auth_tokens.push_back(auth_token);
  watch_call_min_timestamps.push_back(min_timestamp);
  watcher = watcher_ptr;
  DeliverRemoteCommits();
}

void TestPageCloudHandler::UnwatchCommits(CommitWatcher* /*watcher*/) {
  watcher = nullptr;
  watcher_removed = true;
}

void TestPageCloudHandler::GetCommits(
    const std::string& auth_token,
    const std::string& /*min_timestamp*/,
    std::function<void(Status, std::vector<Record>)> callback) {
  get_commits_calls++;
  get_commits_auth_tokens.push_back(auth_token);
  if (should_fail_get_commits) {
    task_runner_->PostTask(
        [callback]() { callback(Status::NETWORK_ERROR, {}); });
    return;
  }

  task_runner_->PostTask([this, callback]() {
    callback(Status::OK, std::move(records_to_return));
  });
}

void TestPageCloudHandler::GetObject(
    const std::string& auth_token,
    ObjectIdView object_id,
    std::function<void(Status status, uint64_t size, zx::socket data)>
        callback) {
  get_object_calls++;
  get_object_auth_tokens.push_back(auth_token);
  if (should_fail_get_object) {
    task_runner_->PostTask(
        [callback]() { callback(Status::NETWORK_ERROR, 0, zx::socket()); });
    return;
  }

  task_runner_->PostTask(
      [ this, object_id = object_id.ToString(), callback ]() {
        callback(Status::OK, objects_to_return[object_id].size(),
                 fsl::WriteStringToSocket(objects_to_return[object_id]));
      });
}

}  // namespace test
}  // namespace cloud_provider_firebase
