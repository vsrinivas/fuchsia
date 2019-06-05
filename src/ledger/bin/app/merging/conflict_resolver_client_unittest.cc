// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/backoff/testing/test_backoff.h>
#include <lib/callback/cancellable_helper.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/fit/function.h>
#include <zircon/errors.h>

#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/merging/custom_merge_strategy.h"
#include "src/ledger/bin/app/merging/merge_resolver.h"
#include "src/ledger/bin/app/merging/test_utils.h"
#include "src/ledger/bin/encryption/primitives/hash.h"
#include "src/ledger/bin/storage/impl/page_storage_impl.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/macros.h"

namespace ledger {
namespace {
class ConflictResolverClientTest : public TestWithPageStorage {
 public:
  ConflictResolverClientTest() {}
  ~ConflictResolverClientTest() override {}

 protected:
  storage::PageStorage* page_storage() override { return page_storage_; }

  void SetUp() override {
    TestWithPageStorage::SetUp();
    std::unique_ptr<storage::PageStorage> page_storage;
    ASSERT_TRUE(CreatePageStorage(&page_storage));
    page_storage_ = page_storage.get();

    std::unique_ptr<MergeResolver> resolver = std::make_unique<MergeResolver>(
        [] {}, &environment_, page_storage_,
        std::make_unique<backoff::TestBackoff>());
    resolver->SetMergeStrategy(nullptr);
    resolver->set_on_empty(QuitLoopClosure());
    merge_resolver_ = resolver.get();

    active_page_manager_ = std::make_unique<ActivePageManager>(
        &environment_, std::move(page_storage), nullptr, std::move(resolver),
        ActivePageManager::PageStorageState::NEEDS_SYNC);
  }

  storage::CommitId CreateCommit(
      storage::CommitIdView parent_id,
      fit::function<void(storage::Journal*)> contents) {
    Status status;
    bool called;
    std::unique_ptr<const storage::Commit> base;
    page_storage_->GetCommit(
        parent_id,
        callback::Capture(callback::SetWhenCalled(&called), &status, &base));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(Status::OK, status);

    std::unique_ptr<storage::Journal> journal =
        page_storage_->StartCommit(std::move(base));

    contents(journal.get());
    std::unique_ptr<const storage::Commit> commit;
    page_storage_->CommitJournal(
        std::move(journal),
        callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    return commit->GetId();
  }

  storage::PageStorage* page_storage_;
  MergeResolver* merge_resolver_;

 private:
  std::unique_ptr<ActivePageManager> active_page_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ConflictResolverClientTest);
};

class ConflictResolverImpl : public ConflictResolver {
 public:
  explicit ConflictResolverImpl(
      fidl::InterfaceRequest<ConflictResolver> request,
      fit::closure quit_callback)
      : binding_(this, std::move(request)),
        quit_callback_(std::move(quit_callback)) {
    binding_.set_error_handler(
        [this](zx_status_t status) { this->disconnected = true; });
  }
  ~ConflictResolverImpl() override {}

  struct ResolveRequest {
    fidl::InterfaceHandle<PageSnapshot> left_version;
    fidl::InterfaceHandle<PageSnapshot> right_version;
    fidl::InterfaceHandle<PageSnapshot> common_version;
    MergeResultProviderPtr result_provider_ptr;
    bool result_provider_disconnected = false;
    zx_status_t result_provider_status = ZX_OK;

    ResolveRequest(fidl::InterfaceHandle<PageSnapshot> left_version,
                   fidl::InterfaceHandle<PageSnapshot> right_version,
                   fidl::InterfaceHandle<PageSnapshot> common_version,
                   fidl::InterfaceHandle<MergeResultProvider> result_provider)
        : left_version(std::move(left_version)),
          right_version(std::move(right_version)),
          common_version(std::move(common_version)),
          result_provider_ptr(result_provider.Bind()) {
      result_provider_ptr.set_error_handler(callback::Capture(
          callback::SetWhenCalled(&result_provider_disconnected),
          &result_provider_status));
    }
  };

  std::vector<std::unique_ptr<ResolveRequest>> requests;
  bool disconnected = false;

 private:
  // ConflictResolver:
  void Resolve(
      fidl::InterfaceHandle<PageSnapshot> left_version,
      fidl::InterfaceHandle<PageSnapshot> right_version,
      fidl::InterfaceHandle<PageSnapshot> common_version,
      fidl::InterfaceHandle<MergeResultProvider> result_provider) override {
    requests.push_back(std::make_unique<ResolveRequest>(
        std::move(left_version), std::move(right_version),
        std::move(common_version), std::move(result_provider)));
    quit_callback_();
  }

  fidl::Binding<ConflictResolver> binding_;
  fit::closure quit_callback_;
};

TEST_F(ConflictResolverClientTest, Error) {
  // Set up conflict.
  CreateCommit(storage::kFirstPageCommitId,
               AddKeyValueToJournal("key1", "value1"));
  CreateCommit(storage::kFirstPageCommitId,
               AddKeyValueToJournal("key2", "value2"));

  // Set the resolver.
  ConflictResolverPtr conflict_resolver_ptr;
  ConflictResolverImpl conflict_resolver_impl(
      conflict_resolver_ptr.NewRequest(), QuitLoopClosure());
  std::unique_ptr<CustomMergeStrategy> custom_merge_strategy =
      std::make_unique<CustomMergeStrategy>(std::move(conflict_resolver_ptr));

  bool custom_strategy_error = false;
  custom_merge_strategy->SetOnError([this, &custom_strategy_error]() {
    custom_strategy_error = true;
    QuitLoop();
  });

  merge_resolver_->SetMergeStrategy(std::move(custom_merge_strategy));
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  Status status = page_storage_->GetHeadCommits(&commits);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(2u, commits.size());

  RunLoopUntilIdle();

  EXPECT_FALSE(merge_resolver_->IsEmpty());
  EXPECT_EQ(1u, conflict_resolver_impl.requests.size());

  // Create a bogus conflict resolution.
  std::vector<MergedValue> merged_values;
  {
    MergedValue merged_value;
    merged_value.key = convert::ToArray("unknown_key");
    merged_value.source = ValueSource::RIGHT;
    merged_values.push_back(std::move(merged_value));
  }

  conflict_resolver_impl.requests[0]->result_provider_ptr->Merge(
      std::move(merged_values));
  RunLoopUntilIdle();

  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            conflict_resolver_impl.requests[0]->result_provider_status);
  EXPECT_EQ(2u, conflict_resolver_impl.requests.size());
}

TEST_F(ConflictResolverClientTest, MergeNonConflicting) {
  // Set up conflict.
  CreateCommit(storage::kFirstPageCommitId,
               AddKeyValueToJournal("key1", "value1"));
  CreateCommit(storage::kFirstPageCommitId,
               AddKeyValueToJournal("key2", "value2"));

  // Set the resolver.
  ConflictResolverPtr conflict_resolver_ptr;
  ConflictResolverImpl conflict_resolver_impl(
      conflict_resolver_ptr.NewRequest(), QuitLoopClosure());
  std::unique_ptr<CustomMergeStrategy> custom_merge_strategy =
      std::make_unique<CustomMergeStrategy>(std::move(conflict_resolver_ptr));

  merge_resolver_->SetMergeStrategy(std::move(custom_merge_strategy));

  RunLoopUntilIdle();

  EXPECT_FALSE(merge_resolver_->IsEmpty());
  EXPECT_EQ(1u, conflict_resolver_impl.requests.size());

  conflict_resolver_impl.requests[0]
      ->result_provider_ptr->MergeNonConflictingEntries();
  conflict_resolver_impl.requests[0]->result_provider_ptr->Done();
  RunLoopUntilIdle();
  ASSERT_TRUE(conflict_resolver_impl.requests[0]->result_provider_disconnected);
  EXPECT_EQ(ZX_OK, conflict_resolver_impl.requests[0]->result_provider_status);

  std::vector<std::unique_ptr<const storage::Commit>> commits;
  Status storage_status = page_storage_->GetHeadCommits(&commits);
  EXPECT_EQ(Status::OK, storage_status);
  // The merge happened.
  EXPECT_EQ(1u, commits.size());

  bool called;
  // Let's verify the contents.
  std::unique_ptr<const storage::Commit> commit = std::move(commits[0]);

  storage::Entry key1_entry, key2_entry;
  page_storage_->GetEntryFromCommit(
      *commit, "key1",
      callback::Capture(callback::SetWhenCalled(&called), &storage_status,
                        &key1_entry));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, storage_status);

  page_storage_->GetEntryFromCommit(
      *commit, "key2",
      callback::Capture(callback::SetWhenCalled(&called), &storage_status,
                        &key2_entry));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, storage_status);

  std::string value1, value2;
  GetValue(key1_entry.object_identifier, &value1);
  EXPECT_EQ("value1", value1);
  GetValue(key2_entry.object_identifier, &value2);
  EXPECT_EQ("value2", value2);
}

TEST_F(ConflictResolverClientTest, MergeNonConflictingOrdering) {
  // Set up conflict.
  storage::CommitId base_id = CreateCommit(
      storage::kFirstPageCommitId, AddKeyValueToJournal("key1", "value1"));
  CreateCommit(base_id, AddKeyValueToJournal("key2", "value2"));
  CreateCommit(base_id, AddKeyValueToJournal("key1", "value1bis"));

  // Set the resolver.
  ConflictResolverPtr conflict_resolver_ptr;
  ConflictResolverImpl conflict_resolver_impl(
      conflict_resolver_ptr.NewRequest(), QuitLoopClosure());
  std::unique_ptr<CustomMergeStrategy> custom_merge_strategy =
      std::make_unique<CustomMergeStrategy>(std::move(conflict_resolver_ptr));

  merge_resolver_->SetMergeStrategy(std::move(custom_merge_strategy));

  RunLoopUntilIdle();

  EXPECT_FALSE(merge_resolver_->IsEmpty());
  EXPECT_EQ(1u, conflict_resolver_impl.requests.size());

  std::vector<MergedValue> merged_values;
  {
    MergedValue merged_value;
    merged_value.key = convert::ToArray("key1");
    merged_value.source = ValueSource::RIGHT;
    merged_values.push_back(std::move(merged_value));
  }

  conflict_resolver_impl.requests[0]->result_provider_ptr->Merge(
      std::move(merged_values));
  conflict_resolver_impl.requests[0]
      ->result_provider_ptr->MergeNonConflictingEntries();
  conflict_resolver_impl.requests[0]->result_provider_ptr->Done();
  RunLoopUntilIdle();
  ASSERT_TRUE(conflict_resolver_impl.requests[0]->result_provider_disconnected);
  EXPECT_EQ(ZX_OK, conflict_resolver_impl.requests[0]->result_provider_status);

  std::vector<std::unique_ptr<const storage::Commit>> commits;
  Status storage_status = page_storage_->GetHeadCommits(&commits);
  EXPECT_EQ(Status::OK, storage_status);
  // The merge happened.
  EXPECT_EQ(1u, commits.size());

  // Let's verify the contents.
  std::unique_ptr<const storage::Commit> commit = std::move(commits[0]);
  bool called;
  storage::Entry key1_entry, key2_entry;
  page_storage_->GetEntryFromCommit(
      *commit, "key1",
      callback::Capture(callback::SetWhenCalled(&called), &storage_status,
                        &key1_entry));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, storage_status);

  page_storage_->GetEntryFromCommit(
      *commit, "key2",
      callback::Capture(callback::SetWhenCalled(&called), &storage_status,
                        &key2_entry));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, storage_status);

  std::string value1, value2;
  GetValue(key1_entry.object_identifier, &value1);
  EXPECT_EQ("value1bis", value1);
  GetValue(key2_entry.object_identifier, &value2);
  EXPECT_EQ("value2", value2);
}

}  // namespace
}  // namespace ledger
