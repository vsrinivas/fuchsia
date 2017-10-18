// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_provider/test/test_page_cloud_handler.h"

#include "lib/fsl/socket/strings.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/functional/make_copyable.h"

namespace cloud_provider_firebase {
namespace test {

TestPageCloudHandler::TestPageCloudHandler(
    fxl::RefPtr<fxl::TaskRunner> task_runner)
    : task_runner_(std::move(task_runner)) {}

TestPageCloudHandler::~TestPageCloudHandler() = default;

void TestPageCloudHandler::DeliverRemoteCommits() {
  if (notifications_to_deliver.empty()) {
    return;
  }

  task_runner_->PostTask(fxl::MakeCopyable(
      [this, records = std::move(notifications_to_deliver)]() mutable {
        watcher->OnRemoteCommits(std::move(records));
      }));
}

void TestPageCloudHandler::AddCommits(
    const std::string& /*auth_token*/,
    std::vector<Commit> commits,
    const std::function<void(Status)>& callback) {
  ++add_commits_calls;
  if (status_to_return == Status::OK) {
    std::move(commits.begin(), commits.end(),
              std::back_inserter(received_commits));
  }
  task_runner_->PostTask(
      [status = status_to_return, callback] { callback(status); });
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
  task_runner_->PostTask(
      fxl::MakeCopyable([callback, status = status_to_return,
                         records = std::move(records_to_return)]() mutable {
        callback(status, std::move(records));
      }));
}

void TestPageCloudHandler::AddObject(const std::string& auth_token,
                                     ObjectDigestView object_digest,
                                     zx::vmo data,
                                     std::function<void(Status)> callback) {
  std::string data_str;
  if (!fsl::StringFromVmo(data, &data_str)) {
    task_runner_->PostTask(
        [callback = std::move(callback)] { callback(Status::INTERNAL_ERROR); });
    return;
  }
  added_objects[object_digest.ToString()] = data_str;
  task_runner_->PostTask(
      [status = status_to_return, callback = std::move(callback)] {
        callback(status);
      });
}

void TestPageCloudHandler::GetObject(
    const std::string& auth_token,
    ObjectDigestView object_digest,
    std::function<void(Status status, uint64_t size, zx::socket data)>
        callback) {
  get_object_calls++;
  get_object_auth_tokens.push_back(auth_token);
  if (status_to_return != Status::OK) {
    task_runner_->PostTask([status = status_to_return, callback] {
      callback(status, 0, zx::socket());
    });
    return;
  }

  task_runner_->PostTask(
      [this, object_digest = object_digest.ToString(), callback]() {
        callback(Status::OK, objects_to_return[object_digest].size(),
                 fsl::WriteStringToSocket(objects_to_return[object_digest]));
      });
}

}  // namespace test
}  // namespace cloud_provider_firebase
