// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/page_handler/testing/test_page_cloud_handler.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/fsl/socket/strings.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/functional/make_copyable.h>

namespace cloud_provider_firebase {
TestPageCloudHandler::TestPageCloudHandler(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

TestPageCloudHandler::~TestPageCloudHandler() = default;

void TestPageCloudHandler::DeliverRemoteCommits() {
  if (notifications_to_deliver.empty()) {
    return;
  }

  async::PostTask(
      dispatcher_,
      fxl::MakeCopyable(
          [this, records = std::move(notifications_to_deliver)]() mutable {
            watcher->OnRemoteCommits(std::move(records));
          }));
}

void TestPageCloudHandler::AddCommits(const std::string& /*auth_token*/,
                                      std::vector<Commit> commits,
                                      fit::function<void(Status)> callback) {
  ++add_commits_calls;
  if (status_to_return == Status::OK) {
    std::move(commits.begin(), commits.end(),
              std::back_inserter(received_commits));
  }
  async::PostTask(dispatcher_,
                  [status = status_to_return, callback = std::move(callback)] {
                    callback(status);
                  });
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
    const std::string& auth_token, const std::string& /*min_timestamp*/,
    fit::function<void(Status, std::vector<Record>)> callback) {
  get_commits_calls++;
  get_commits_auth_tokens.push_back(auth_token);
  async::PostTask(
      dispatcher_, fxl::MakeCopyable(
                  [callback = std::move(callback), status = status_to_return,
                   records = std::move(records_to_return)]() mutable {
                    callback(status, std::move(records));
                  }));
}

void TestPageCloudHandler::AddObject(const std::string& auth_token,
                                     ObjectDigestView object_digest,
                                     fsl::SizedVmo data,
                                     fit::function<void(Status)> callback) {
  std::string data_str;
  if (!fsl::StringFromVmo(data, &data_str)) {
    async::PostTask(dispatcher_, [callback = std::move(callback)] {
      callback(Status::INTERNAL_ERROR);
    });
    return;
  }
  added_objects[object_digest.ToString()] = data_str;
  async::PostTask(dispatcher_,
                  [status = status_to_return, callback = std::move(callback)] {
                    callback(status);
                  });
}

void TestPageCloudHandler::GetObject(
    const std::string& auth_token, ObjectDigestView object_digest,
    fit::function<void(Status status, uint64_t size, zx::socket data)>
        callback) {
  get_object_calls++;
  get_object_auth_tokens.push_back(auth_token);
  if (status_to_return != Status::OK) {
    async::PostTask(
        dispatcher_, [status = status_to_return, callback = std::move(callback)] {
          callback(status, 0, zx::socket());
        });
    return;
  }

  async::PostTask(dispatcher_, [this, object_digest = object_digest.ToString(),
                           callback = std::move(callback)]() {
    callback(Status::OK, objects_to_return[object_digest].size(),
             fsl::WriteStringToSocket(objects_to_return[object_digest]));
  });
}
}  // namespace cloud_provider_firebase
