// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_TESTING_TEST_PAGE_CLOUD_H_
#define SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_TESTING_TEST_PAGE_CLOUD_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>

#include "peridot/lib/commit_pack/commit_pack.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/lib/fxl/macros.h"

namespace cloud_sync {

struct ReceivedCommit {
  std::string id;
  std::string data;
};

cloud_provider::CommitPackEntry MakeTestCommit(
    encryption::FakeEncryptionService* encryption_service,
    const std::string& id, const std::string& data);

std::unique_ptr<cloud_provider::CommitPack> MakeTestCommitPack(
    encryption::FakeEncryptionService* encryption_service,
    std::vector<std::tuple<std::string, std::string>> commit_data);

class TestPageCloud : public cloud_provider::PageCloud {
 public:
  explicit TestPageCloud(
      fidl::InterfaceRequest<cloud_provider::PageCloud> request);
  ~TestPageCloud() override;

  void RunPendingCallbacks();
  void Unbind() { binding_.Unbind(); }

  cloud_provider::Status status_to_return = cloud_provider::Status::OK;
  cloud_provider::Status commit_status_to_return = cloud_provider::Status::OK;
  cloud_provider::Status object_status_to_return = cloud_provider::Status::OK;

  // AddCommits().
  unsigned int add_commits_calls = 0u;
  std::vector<cloud_provider::CommitPackEntry> received_commits;

  // GetCommits().
  unsigned int get_commits_calls = 0u;
  std::vector<cloud_provider::CommitPackEntry> commits_to_return;
  std::unique_ptr<cloud_provider::PositionToken> position_token_to_return;

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
  std::vector<std::unique_ptr<cloud_provider::PositionToken>>
      set_watcher_position_tokens;
  cloud_provider::PageCloudWatcherPtr set_watcher;

 private:
  // cloud_provider::PageCloud:
  void AddCommits(cloud_provider::CommitPack commits,
                  AddCommitsCallback callback) override;
  void GetCommits(
      std::unique_ptr<cloud_provider::PositionToken> min_position_token,
      GetCommitsCallback callback) override;
  void AddObject(std::vector<uint8_t> id, fuchsia::mem::Buffer data,
                 AddObjectCallback callback) override;
  void GetObject(std::vector<uint8_t> id, GetObjectCallback callback) override;
  void SetWatcher(
      std::unique_ptr<cloud_provider::PositionToken> min_position_token,
      fidl::InterfaceHandle<cloud_provider::PageCloudWatcher> watcher,
      SetWatcherCallback callback) override;

  fidl::Binding<cloud_provider::PageCloud> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestPageCloud);
};

}  // namespace cloud_sync

#endif  // SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_TESTING_TEST_PAGE_CLOUD_H_
