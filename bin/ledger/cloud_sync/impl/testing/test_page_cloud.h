// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_TESTING_TEST_PAGE_CLOUD_H_
#define PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_TESTING_TEST_PAGE_CLOUD_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/fidl/cpp/array.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/ledger/encryption/fake/fake_encryption_service.h"
#include "peridot/bin/ledger/fidl/include/types.h"

namespace cloud_sync {

struct ReceivedCommit {
  std::string id;
  std::string data;
};

cloud_provider::Commit MakeTestCommit(
    encryption::FakeEncryptionService* encryption_service,
    const std::string& id, const std::string& data);

class TestPageCloud : public cloud_provider::PageCloud {
 public:
  explicit TestPageCloud(
      fidl::InterfaceRequest<cloud_provider::PageCloud> request);
  ~TestPageCloud() override;

  void RunPendingCallbacks();

  cloud_provider::Status status_to_return = cloud_provider::Status::OK;
  cloud_provider::Status commit_status_to_return = cloud_provider::Status::OK;
  cloud_provider::Status object_status_to_return = cloud_provider::Status::OK;

  // AddCommits().
  unsigned int add_commits_calls = 0u;
  std::vector<ReceivedCommit> received_commits;

  // GetCommits().
  unsigned int get_commits_calls = 0u;
  fidl::VectorPtr<cloud_provider::Commit> commits_to_return =
      fidl::VectorPtr<cloud_provider::Commit>::New(0);
  std::unique_ptr<cloud_provider::Token> position_token_to_return;

  // AddObject().
  unsigned int add_object_calls = 0u;
  std::map<std::string, std::string> received_objects;
  bool delay_add_object_callbacks = false;
  std::vector<fit::closure> pending_add_object_callbacks;
  bool reset_object_status_after_call = false;

  // GetObject().
  unsigned int get_object_calls = 0u;
  std::map<std::string, std::string> objects_to_return;

  // SetWatcher().
  std::vector<std::unique_ptr<cloud_provider::Token>>
      set_watcher_position_tokens;
  cloud_provider::PageCloudWatcherPtr set_watcher;

 private:
  // cloud_provider::PageCloud:
  void AddCommits(fidl::VectorPtr<cloud_provider::Commit> commits,
                  AddCommitsCallback callback) override;
  void GetCommits(std::unique_ptr<cloud_provider::Token> min_position_token,
                  GetCommitsCallback callback) override;
  void AddObject(fidl::VectorPtr<uint8_t> id, fuchsia::mem::Buffer data,
                 AddObjectCallback callback) override;
  void GetObject(fidl::VectorPtr<uint8_t> id,
                 GetObjectCallback callback) override;
  void SetWatcher(
      std::unique_ptr<cloud_provider::Token> min_position_token,
      fidl::InterfaceHandle<cloud_provider::PageCloudWatcher> watcher,
      SetWatcherCallback callback) override;

  fidl::Binding<cloud_provider::PageCloud> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestPageCloud);
};

}  // namespace cloud_sync

#endif  // PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_TESTING_TEST_PAGE_CLOUD_H_
