// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_PROVIDER_TEST_TEST_PAGE_CLOUD_HANDLER_H_
#define APPS_LEDGER_SRC_CLOUD_PROVIDER_TEST_TEST_PAGE_CLOUD_HANDLER_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "apps/ledger/src/cloud_provider/public/page_cloud_handler.h"
#include "apps/ledger/src/cloud_provider/test/page_cloud_handler_empty_impl.h"
#include "garnet/public/lib/fxl/tasks/task_runner.h"
#include "lib/fxl/macros.h"

namespace cloud_provider_firebase {
namespace test {

// Fake implementation of cloud_provider_firebase::PageCloudHandler.
//
// Registers for inspection the calls made on it and injects the returned status
// for individual methods allowing the test to verify error error handling.
class TestPageCloudHandler
    : public cloud_provider_firebase::test::PageCloudHandlerEmptyImpl {
 public:
  explicit TestPageCloudHandler(fxl::RefPtr<fxl::TaskRunner> task_runner);

  ~TestPageCloudHandler() override;

  void DeliverRemoteCommits();

  // PageCloudHandler:
  void AddCommits(const std::string& /*auth_token*/,
                  std::vector<cloud_provider_firebase::Commit> commits,
                  const std::function<void(cloud_provider_firebase::Status)>&
                      callback) override;

  void WatchCommits(
      const std::string& auth_token,
      const std::string& min_timestamp,
      cloud_provider_firebase::CommitWatcher* watcher_ptr) override;

  void UnwatchCommits(
      cloud_provider_firebase::CommitWatcher* /*watcher*/) override;

  void GetCommits(
      const std::string& auth_token,
      const std::string& /*min_timestamp*/,
      std::function<void(cloud_provider_firebase::Status,
                         std::vector<cloud_provider_firebase::Record>)>
          callback) override;

  void GetObject(const std::string& auth_token,
                 cloud_provider_firebase::ObjectIdView object_id,
                 std::function<void(cloud_provider_firebase::Status status,
                                    uint64_t size,
                                    zx::socket data)> callback) override;

  bool should_fail_get_commits = false;
  bool should_fail_get_object = false;
  std::vector<cloud_provider_firebase::Record> records_to_return;
  std::vector<cloud_provider_firebase::Record> notifications_to_deliver;
  cloud_provider_firebase::Status commit_status_to_return =
      cloud_provider_firebase::Status::OK;
  std::unordered_map<std::string, std::string> objects_to_return;

  std::vector<std::string> watch_commits_auth_tokens;
  std::vector<std::string> watch_call_min_timestamps;
  unsigned int add_commits_calls = 0u;
  unsigned int get_commits_calls = 0u;
  std::vector<std::string> get_commits_auth_tokens;
  unsigned int get_object_calls = 0u;
  std::vector<std::string> get_object_auth_tokens;
  std::vector<cloud_provider_firebase::Commit> received_commits;
  bool watcher_removed = false;
  cloud_provider_firebase::CommitWatcher* watcher = nullptr;

 private:
  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestPageCloudHandler);
};

}  // namespace test

}  // namespace cloud_provider_firebase

#endif  // APPS_LEDGER_SRC_CLOUD_PROVIDER_TEST_TEST_PAGE_CLOUD_HANDLER_H_
