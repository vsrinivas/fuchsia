// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CLOUD_PROVIDER_TEST_TEST_PAGE_CLOUD_HANDLER_H_
#define PERIDOT_BIN_LEDGER_CLOUD_PROVIDER_TEST_TEST_PAGE_CLOUD_HANDLER_H_

#include <map>
#include <string>
#include <vector>

#include "garnet/public/lib/fxl/tasks/task_runner.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/cloud_provider/public/page_cloud_handler.h"
#include "peridot/bin/ledger/cloud_provider/test/page_cloud_handler_empty_impl.h"

namespace cloud_provider_firebase {
namespace test {

// Fake implementation of PageCloudHandler.
//
// Registers for inspection the calls made on it and injects the returned status
// for individual methods allowing the test to verify error error handling.
class TestPageCloudHandler : public PageCloudHandlerEmptyImpl {
 public:
  explicit TestPageCloudHandler(fxl::RefPtr<fxl::TaskRunner> task_runner);

  ~TestPageCloudHandler() override;

  void DeliverRemoteCommits();

  // PageCloudHandler:
  void AddCommits(const std::string& auth_token,
                  std::vector<Commit> commits,
                  const std::function<void(Status)>& callback) override;

  void WatchCommits(const std::string& auth_token,
                    const std::string& min_timestamp,
                    CommitWatcher* watcher_ptr) override;

  void UnwatchCommits(CommitWatcher* watcher) override;

  void GetCommits(
      const std::string& auth_token,
      const std::string& min_timestamp,
      std::function<void(Status, std::vector<Record>)> callback) override;

  void AddObject(const std::string& auth_token,
                 ObjectIdView object_id,
                 zx::vmo data,
                 std::function<void(Status)> callback) override;

  void GetObject(
      const std::string& auth_token,
      ObjectIdView object_id,
      std::function<void(Status status, uint64_t size, zx::socket data)>
          callback) override;

  std::vector<Record> records_to_return;
  std::vector<Record> notifications_to_deliver;
  Status status_to_return = Status::OK;
  std::map<std::string, std::string> objects_to_return;
  std::map<std::string, std::string> added_objects;

  std::vector<std::string> watch_commits_auth_tokens;
  std::vector<std::string> watch_call_min_timestamps;
  unsigned int add_commits_calls = 0u;
  unsigned int get_commits_calls = 0u;
  std::vector<std::string> get_commits_auth_tokens;
  unsigned int get_object_calls = 0u;
  std::vector<std::string> get_object_auth_tokens;
  std::vector<Commit> received_commits;
  bool watcher_removed = false;
  CommitWatcher* watcher = nullptr;

 private:
  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestPageCloudHandler);
};

}  // namespace test

}  // namespace cloud_provider_firebase

#endif  // PERIDOT_BIN_LEDGER_CLOUD_PROVIDER_TEST_TEST_PAGE_CLOUD_HANDLER_H_
