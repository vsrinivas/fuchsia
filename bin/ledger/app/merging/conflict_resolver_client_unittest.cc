// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/merging/merge_resolver.h"

#include <memory>
#include <string>

#include "apps/ledger/src/app/constants.h"
#include "apps/ledger/src/app/merging/custom_merge_strategy.h"
#include "apps/ledger/src/app/merging/test_utils.h"
#include "apps/ledger/src/callback/cancellable_helper.h"
#include "apps/ledger/src/callback/capture.h"
#include "apps/ledger/src/coroutine/coroutine_impl.h"
#include "apps/ledger/src/glue/crypto/hash.h"
#include "apps/ledger/src/storage/impl/page_storage_impl.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/macros.h"

namespace ledger {
namespace {
class ConflictResolverClientTest : public test::TestWithPageStorage {
 public:
  ConflictResolverClientTest()
      : environment_(message_loop_.task_runner(),
                     nullptr,
                     message_loop_.task_runner()) {}
  ~ConflictResolverClientTest() override {}

 protected:
  storage::PageStorage* page_storage() override { return page_storage_; }

  void SetUp() override {
    ::testing::Test::SetUp();
    std::unique_ptr<storage::PageStorage> page_storage;
    ASSERT_TRUE(CreatePageStorage(&page_storage));
    page_storage_ = page_storage.get();

    std::unique_ptr<MergeResolver> resolver = std::make_unique<MergeResolver>(
        [] {}, &environment_, page_storage_,
        std::make_unique<test::TestBackoff>(nullptr));
    resolver->SetMergeStrategy(nullptr);
    resolver->set_on_empty(MakeQuitTask());
    merge_resolver_ = resolver.get();

    page_manager_ = std::make_unique<PageManager>(
        &environment_, std::move(page_storage), nullptr, std::move(resolver),
        PageManager::PageStorageState::NEW);
  }

  storage::CommitId CreateCommit(
      storage::CommitIdView parent_id,
      std::function<void(storage::Journal*)> contents) {
    storage::Status status;
    std::unique_ptr<storage::Journal> journal;
    page_storage_->StartCommit(
        parent_id.ToString(), storage::JournalType::IMPLICIT,
        callback::Capture(MakeQuitTask(), &status, &journal));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(storage::Status::OK, status);

    contents(journal.get());
    std::unique_ptr<const storage::Commit> commit;
    page_storage_->CommitJournal(
        std::move(journal),
        callback::Capture(MakeQuitTask(), &status, &commit));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(storage::Status::OK, status);
    return commit->GetId();
  }

  storage::PageStorage* page_storage_;
  MergeResolver* merge_resolver_;

 private:
  Environment environment_;
  std::unique_ptr<PageManager> page_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ConflictResolverClientTest);
};

class ConflictResolverImpl : public ConflictResolver {
 public:
  explicit ConflictResolverImpl(
      fidl::InterfaceRequest<ConflictResolver> request)
      : binding_(this, std::move(request)) {
    binding_.set_connection_error_handler(
        [this] { this->disconnected = true; });
  }
  ~ConflictResolverImpl() override {}

  struct ResolveRequest {
    fidl::InterfaceHandle<PageSnapshot> left_version;
    fidl::InterfaceHandle<PageSnapshot> right_version;
    fidl::InterfaceHandle<PageSnapshot> common_version;
    MergeResultProviderPtr result_provider_ptr;
    bool result_provider_disconnected = false;

    ResolveRequest(fidl::InterfaceHandle<PageSnapshot> left_version,
                   fidl::InterfaceHandle<PageSnapshot> right_version,
                   fidl::InterfaceHandle<PageSnapshot> common_version,
                   fidl::InterfaceHandle<MergeResultProvider> result_provider)
        : left_version(std::move(left_version)),
          right_version(std::move(right_version)),
          common_version(std::move(common_version)),
          result_provider_ptr(
              MergeResultProviderPtr::Create(std::move(result_provider))) {
      result_provider_ptr.set_connection_error_handler(
          [this] { result_provider_disconnected = true; });
    }
  };

  std::vector<ResolveRequest> requests;
  bool disconnected = false;

 private:
  // ConflictResolver:
  void Resolve(
      fidl::InterfaceHandle<PageSnapshot> left_version,
      fidl::InterfaceHandle<PageSnapshot> right_version,
      fidl::InterfaceHandle<PageSnapshot> common_version,
      fidl::InterfaceHandle<MergeResultProvider> result_provider) override {
    requests.emplace_back(std::move(left_version), std::move(right_version),
                          std::move(common_version),
                          std::move(result_provider));
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
  }

  fidl::Binding<ConflictResolver> binding_;
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
      conflict_resolver_ptr.NewRequest());
  std::unique_ptr<CustomMergeStrategy> custom_merge_strategy =
      std::make_unique<CustomMergeStrategy>(std::move(conflict_resolver_ptr));

  bool custom_strategy_error = false;
  custom_merge_strategy->SetOnError([&custom_strategy_error]() {
    custom_strategy_error = true;
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
  });

  merge_resolver_->SetMergeStrategy(std::move(custom_merge_strategy));
  storage::Status status;
  std::vector<storage::CommitId> ids;
  page_storage_->GetHeadCommitIds(
      callback::Capture(MakeQuitTask(), &status, &ids));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(2u, ids.size());

  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_FALSE(merge_resolver_->IsEmpty());
  EXPECT_EQ(1u, conflict_resolver_impl.requests.size());

  // Create a bogus conflict resolution.
  fidl::Array<MergedValuePtr> merged_values =
      fidl::Array<MergedValuePtr>::New(0);
  {
    MergedValuePtr merged_value = MergedValue::New();
    merged_value->key = convert::ToArray("unknown_key");
    merged_value->source = ValueSource::RIGHT;
    merged_values.push_back(std::move(merged_value));
  }

  Status merge_status;
  conflict_resolver_impl.requests[0].result_provider_ptr->Merge(
      std::move(merged_values),
      callback::Capture(MakeQuitTask(), &merge_status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::KEY_NOT_FOUND, merge_status);

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(conflict_resolver_impl.requests[0].result_provider_disconnected);
  EXPECT_EQ(2u, conflict_resolver_impl.requests.size());
}

}  // namespace
}  // namespace ledger
