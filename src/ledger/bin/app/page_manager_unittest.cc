// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_manager.h"

#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <cstdint>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/disk_cleanup_manager_impl.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/inspect/inspect.h"
#include "src/ledger/bin/storage/fake/fake_db_factory.h"
#include "src/ledger/bin/storage/fake/fake_ledger_storage.h"
#include "src/ledger/bin/storage/fake/fake_page_storage.h"
#include "src/ledger/bin/storage/impl/ledger_storage_impl.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/ledger_storage.h"
#include "src/ledger/bin/sync_coordinator/public/ledger_sync.h"
#include "src/ledger/bin/sync_coordinator/testing/fake_ledger_sync.h"
#include "src/ledger/bin/testing/fake_disk_cleanup_manager.h"
#include "src/ledger/bin/testing/inspect.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"
#include "src/lib/callback/waiter.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/inspect_deprecated/inspect.h"

namespace ledger {
namespace {

constexpr fxl::StringView kLedgerName = "ledger_under_test";
constexpr fxl::StringView kTestTopLevelNodeName = "top-level-of-test node";

class PageManagerTest : public TestWithEnvironment {
 public:
  PageManagerTest() = default;
  PageManagerTest(const PageManagerTest&) = delete;
  PageManagerTest& operator=(const PageManagerTest&) = delete;

  ~PageManagerTest() override = default;

  // gtest::TestWithEnvironment:
  void SetUp() override {
    TestWithEnvironment::SetUp();
    page_id_ = RandomId();
    top_level_node_ = inspect_deprecated::Node(kTestTopLevelNodeName.ToString());
    attachment_node_ =
        top_level_node_.CreateChild(kSystemUnderTestAttachmentPointPathComponent.ToString());
    ledger_merge_manager_ = std::make_unique<LedgerMergeManager>(&environment_);
    storage_ = std::make_unique<storage::fake::FakeLedgerStorage>(&environment_);
    sync_ = std::make_unique<sync_coordinator::FakeLedgerSync>();
    disk_cleanup_manager_ = std::make_unique<FakeDiskCleanupManager>();
    page_manager_ = std::make_unique<PageManager>(
        &environment_, kLedgerName.ToString(), convert::ToString(page_id_.id),
        std::vector<PageUsageListener*>{disk_cleanup_manager_.get()}, storage_.get(), sync_.get(),
        ledger_merge_manager_.get(), attachment_node_.CreateChild(convert::ToString(page_id_.id)));
  }

  PageId RandomId() {
    PageId result;
    environment_.random()->Draw(&result.id);
    return result;
  }

 protected:
  // TODO(nathaniel): Because we use the ChildrenManager API, we need to do our reads using FIDL,
  // and because we want to use inspect::ReadFromFidl for our reads, we need to have these two
  // objects (one parent, one child, both part of the test, and with the system under test attaching
  // to the child) rather than just one. Even though this is test code this is still a layer of
  // indirection that should be eliminable in Inspect's upcoming "VMO-World".
  inspect_deprecated::Node top_level_node_;
  inspect_deprecated::Node attachment_node_;
  std::unique_ptr<storage::fake::FakeLedgerStorage> storage_;
  std::unique_ptr<sync_coordinator::FakeLedgerSync> sync_;
  std::unique_ptr<LedgerMergeManager> ledger_merge_manager_;
  std::unique_ptr<FakeDiskCleanupManager> disk_cleanup_manager_;
  std::unique_ptr<PageManager> page_manager_;
  PageId page_id_;
};

class StubConflictResolverFactory : public ConflictResolverFactory {
 public:
  explicit StubConflictResolverFactory(fidl::InterfaceRequest<ConflictResolverFactory> request)
      : binding_(this, std::move(request)) {
    binding_.set_error_handler([this](zx_status_t status) { disconnected = true; });
  }

  bool disconnected = false;

 private:
  void GetPolicy(PageId page_id, fit::function<void(MergePolicy)> callback) override {}

  void NewConflictResolver(PageId page_id,
                           fidl::InterfaceRequest<ConflictResolver> resolver) override {}

  fidl::Binding<ConflictResolverFactory> binding_;
};

TEST_F(PageManagerTest, OnDiscardableCalled) {
  bool get_page_callback_called;
  Status get_page_status;
  bool on_discardable_called;

  page_manager_->SetOnDiscardable(
      callback::Capture(callback::SetWhenCalled(&on_discardable_called)));

  PagePtr page;
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NEW, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(get_page_status, Status::OK);
  EXPECT_FALSE(on_discardable_called);

  fit::closure detacher = page_manager_->CreateDetacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  page.Unbind();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  detacher();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(PageManagerTest, OnDiscardableCalledWhenHeadDetacherCalled) {
  bool get_page_callback_called;
  Status get_page_status;
  bool on_discardable_called;

  page_manager_->SetOnDiscardable(
      callback::Capture(callback::SetWhenCalled(&on_discardable_called)));

  PagePtr page;
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NEW, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(Status::OK, get_page_status);
  EXPECT_FALSE(on_discardable_called);

  fit::closure page_detacher = page_manager_->CreateDetacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  page.Unbind();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> page_node;
  EXPECT_TRUE(
      OpenChild(&attachment_node_, convert::ToString(page_id_.id), &page_node, &test_loop()));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> heads_node;
  EXPECT_TRUE(
      OpenChild(&page_node, kHeadsInspectPathComponent.ToString(), &heads_node, &test_loop()));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> head_node;
  EXPECT_TRUE(OpenChild(&heads_node, CommitIdToDisplayName(storage::kFirstPageCommitId.ToString()),
                        &head_node, &test_loop()));

  page_detacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  page_node.Unbind();
  heads_node.Unbind();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  head_node.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(PageManagerTest, OnDiscardableCalledWhenCommitDetacherCalled) {
  bool get_page_callback_called;
  Status get_page_status;
  bool on_discardable_called;

  page_manager_->SetOnDiscardable(
      callback::Capture(callback::SetWhenCalled(&on_discardable_called)));

  PagePtr page;
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NEW, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(Status::OK, get_page_status);
  EXPECT_FALSE(on_discardable_called);

  fit::closure page_detacher = page_manager_->CreateDetacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  page.Unbind();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> page_node;
  EXPECT_TRUE(
      OpenChild(&attachment_node_, convert::ToString(page_id_.id), &page_node, &test_loop()));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> commits_node;
  EXPECT_TRUE(
      OpenChild(&page_node, kHeadsInspectPathComponent.ToString(), &commits_node, &test_loop()));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> commit_node;
  EXPECT_TRUE(OpenChild(&commits_node,
                        CommitIdToDisplayName(storage::kFirstPageCommitId.ToString()), &commit_node,
                        &test_loop()));

  page_detacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  page_node.Unbind();
  commits_node.Unbind();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  commit_node.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(PageManagerTest, OnDiscardableCalledInspectEarlierAndLaterThanPageBinding) {
  bool get_page_callback_called;
  Status get_page_status;
  bool on_discardable_called;

  page_manager_->SetOnDiscardable(
      callback::Capture(callback::SetWhenCalled(&on_discardable_called)));

  PagePtr page;
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NEW, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(Status::OK, get_page_status);
  EXPECT_FALSE(on_discardable_called);

  page.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);

  page_manager_->SetOnDiscardable(
      callback::Capture(callback::SetWhenCalled(&on_discardable_called)));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> page_node;
  EXPECT_TRUE(
      OpenChild(&attachment_node_, convert::ToString(page_id_.id), &page_node, &test_loop()));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> heads_node;
  EXPECT_TRUE(
      OpenChild(&page_node, kHeadsInspectPathComponent.ToString(), &heads_node, &test_loop()));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> head_node;
  EXPECT_TRUE(OpenChild(&heads_node, CommitIdToDisplayName(storage::kFirstPageCommitId.ToString()),
                        &head_node, &test_loop()));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> commits_node;
  EXPECT_TRUE(
      OpenChild(&page_node, kHeadsInspectPathComponent.ToString(), &commits_node, &test_loop()));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> commit_node;
  EXPECT_TRUE(OpenChild(&commits_node,
                        CommitIdToDisplayName(storage::kFirstPageCommitId.ToString()), &commit_node,
                        &test_loop()));

  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NEW, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(Status::OK, get_page_status);
  EXPECT_FALSE(on_discardable_called);

  page.Unbind();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  head_node.Unbind();
  commit_node.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);

  // Why didn't we have to unbind page_node, heads_node, or commits_node?
  //
  // For page_node: because within the scope of this unit test the Page is the system under test, so
  // there's no LedgerManager-acting-as-a-ChildrenManager using an active inspection as a reason to
  // keep a PageManager non-empty. Outside of the scope of this test, in the scope of a real running
  // integrated system, an active inspection of a Page serves to keep a PageManager non-empty.
  //
  // For heads_node and commits_node: an active inspection of a heads node would never serve to keep
  // a Page non-empty (applications under inspection retain the right to remove nodes from their
  // hierarchies at any time) but inspections always maintain connections to parents for the
  // entirety of their connections to children, so any time heads_node is bound in a real integrated
  // system, page_node is also bound.
}

TEST_F(PageManagerTest, OnDiscardableCalledInspectEarlierAndPageBindingLater) {
  bool get_page_callback_called;
  Status get_page_status;
  bool on_discardable_called;

  page_manager_->SetOnDiscardable(
      callback::Capture(callback::SetWhenCalled(&on_discardable_called)));

  PagePtr page;
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NEW, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(Status::OK, get_page_status);
  EXPECT_FALSE(on_discardable_called);

  page.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);

  page_manager_->SetOnDiscardable(
      callback::Capture(callback::SetWhenCalled(&on_discardable_called)));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> page_node;
  EXPECT_TRUE(
      OpenChild(&attachment_node_, convert::ToString(page_id_.id), &page_node, &test_loop()));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> heads_node;
  EXPECT_TRUE(
      OpenChild(&page_node, kHeadsInspectPathComponent.ToString(), &heads_node, &test_loop()));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> head_node;
  EXPECT_TRUE(OpenChild(&heads_node, CommitIdToDisplayName(storage::kFirstPageCommitId.ToString()),
                        &head_node, &test_loop()));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> commits_node;
  EXPECT_TRUE(
      OpenChild(&page_node, kHeadsInspectPathComponent.ToString(), &commits_node, &test_loop()));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> commit_node;
  EXPECT_TRUE(OpenChild(&commits_node,
                        CommitIdToDisplayName(storage::kFirstPageCommitId.ToString()), &commit_node,
                        &test_loop()));

  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NEW, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(Status::OK, get_page_status);
  EXPECT_FALSE(on_discardable_called);

  heads_node.Unbind();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  commit_node.Unbind();
  head_node.Unbind();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  page.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);

  // Why didn't we have to unbind page_node, heads_node, or commits_node?
  //
  // For page_node: because within the scope of this unit test the Page is the system under test, so
  // there's no LedgerManager-acting-as-a-ChildrenManager using an active inspection as a reason to
  // keep a PageManager non-empty. Outside of the scope of this test, in the scope of a real running
  // integrated system, an active inspection of a Page serves to keep a PageManager non-empty.
  //
  // For heads_node and commits_node: an active inspection of a heads node would never serve to keep
  // a Page non-empty (applications under inspection retain the right to remove nodes from their
  // hierarchies at any time) but inspections always maintain connections to parents for the
  // entirety of their connections to children, so any time heads_node is bound in a real integrated
  // system, page_node is also bound.
}

TEST_F(PageManagerTest, OnDiscardableCalledPageBindingEarlierAndInspectLater) {
  bool get_page_callback_called;
  Status get_page_status;
  bool on_discardable_called;

  page_manager_->SetOnDiscardable(
      callback::Capture(callback::SetWhenCalled(&on_discardable_called)));

  PagePtr page;
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NEW, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(Status::OK, get_page_status);
  EXPECT_FALSE(on_discardable_called);

  page.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);

  page_manager_->SetOnDiscardable(
      callback::Capture(callback::SetWhenCalled(&on_discardable_called)));

  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NEW, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(Status::OK, get_page_status);
  EXPECT_FALSE(on_discardable_called);

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> page_node;
  EXPECT_TRUE(
      OpenChild(&attachment_node_, convert::ToString(page_id_.id), &page_node, &test_loop()));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> heads_node;
  EXPECT_TRUE(
      OpenChild(&page_node, kHeadsInspectPathComponent.ToString(), &heads_node, &test_loop()));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> head_node;
  EXPECT_TRUE(OpenChild(&heads_node, CommitIdToDisplayName(storage::kFirstPageCommitId.ToString()),
                        &head_node, &test_loop()));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> commits_node;
  EXPECT_TRUE(
      OpenChild(&page_node, kHeadsInspectPathComponent.ToString(), &commits_node, &test_loop()));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> commit_node;
  EXPECT_TRUE(OpenChild(&commits_node,
                        CommitIdToDisplayName(storage::kFirstPageCommitId.ToString()), &commit_node,
                        &test_loop()));

  page.Unbind();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  head_node.Unbind();
  commit_node.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);

  // Why didn't we have to unbind page_node, heads_node, or commits_node?
  //
  // For page_node: because within the scope of this unit test the Page is the system under test, so
  // there's no LedgerManager-acting-as-a-ChildrenManager using an active inspection as a reason to
  // keep a PageManager non-empty. Outside of the scope of this test, in the scope of a real running
  // integrated system, an active inspection of a Page serves to keep a PageManager non-empty.
  //
  // For heads_node and commits_node: an active inspection of a heads node would never serve to keep
  // a Page non-empty (applications under inspection retain the right to remove nodes from their
  // hierarchies at any time) but inspections always maintain connections to parents for the
  // entirety of their connections to children, so any time heads_node is bound in a real integrated
  // system, page_node is also bound.
}

TEST_F(PageManagerTest, OnDiscardableCalledPageBindingEarlierAndLaterThanInspect) {
  bool get_page_callback_called;
  Status get_page_status;
  bool on_discardable_called;

  page_manager_->SetOnDiscardable(
      callback::Capture(callback::SetWhenCalled(&on_discardable_called)));

  PagePtr page;
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NEW, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(Status::OK, get_page_status);
  EXPECT_FALSE(on_discardable_called);

  page.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);

  page_manager_->SetOnDiscardable(
      callback::Capture(callback::SetWhenCalled(&on_discardable_called)));

  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NEW, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(Status::OK, get_page_status);
  EXPECT_FALSE(on_discardable_called);

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> page_node;
  EXPECT_TRUE(
      OpenChild(&attachment_node_, convert::ToString(page_id_.id), &page_node, &test_loop()));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> heads_node;
  EXPECT_TRUE(
      OpenChild(&page_node, kHeadsInspectPathComponent.ToString(), &heads_node, &test_loop()));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> head_node;
  EXPECT_TRUE(OpenChild(&heads_node, CommitIdToDisplayName(storage::kFirstPageCommitId.ToString()),
                        &head_node, &test_loop()));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> commits_node;
  EXPECT_TRUE(
      OpenChild(&page_node, kHeadsInspectPathComponent.ToString(), &commits_node, &test_loop()));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> commit_node;
  EXPECT_TRUE(OpenChild(&commits_node,
                        CommitIdToDisplayName(storage::kFirstPageCommitId.ToString()), &commit_node,
                        &test_loop()));

  commit_node.Unbind();
  head_node.Unbind();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  page.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);

  // Why didn't we have to unbind page_node, heads_node, or commits_node?
  //
  // For page_node: because within the scope of this unit test the Page is the system under test, so
  // there's no LedgerManager-acting-as-a-ChildrenManager using an active inspection as a reason to
  // keep a PageManager non-empty. Outside of the scope of this test, in the scope of a real running
  // integrated system, an active inspection of a Page serves to keep a PageManager non-empty.
  //
  // For heads_node and commits_node: an active inspection of a heads node would never serve to keep
  // a Page non-empty (applications under inspection retain the right to remove nodes from their
  // hierarchies at any time) but inspections always maintain connections to parents for the
  // entirety of their connections to children, so any time heads_node is bound in a real integrated
  // system, page_node is also bound.
}

TEST_F(PageManagerTest, PageIsClosedAndSyncedCheckNotFound) {
  bool called;
  Status status;
  PagePredicateResult is_closed_and_synced;

  // Check for a page that doesn't exist.
  storage_->should_get_page_fail = true;
  page_manager_->PageIsClosedAndSynced(
      callback::Capture(callback::SetWhenCalled(&called), &status, &is_closed_and_synced));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::PAGE_NOT_FOUND);
}

// Check for a page that exists, is synced and open. PageIsClosedAndSynced
// should be false.
TEST_F(PageManagerTest, PageIsClosedAndSyncedCheckClosed) {
  bool get_page_callback_called;
  Status get_page_status;
  bool called;
  PagePredicateResult is_closed_and_synced;

  storage_->should_get_page_fail = false;
  PagePtr page;
  storage::PageIdView storage_page_id = convert::ExtendedStringView(page_id_.id);
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(get_page_status, Status::OK);

  Status storage_status;
  storage_->set_page_storage_synced(storage_page_id, true);
  page_manager_->PageIsClosedAndSynced(
      callback::Capture(callback::SetWhenCalled(&called), &storage_status, &is_closed_and_synced));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage_status, Status::OK);
  EXPECT_EQ(is_closed_and_synced, PagePredicateResult::PAGE_OPENED);

  // Close the page. PageIsClosedAndSynced should now be true.
  page.Unbind();
  RunLoopUntilIdle();

  page_manager_->PageIsClosedAndSynced(
      callback::Capture(callback::SetWhenCalled(&called), &storage_status, &is_closed_and_synced));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage_status, Status::OK);
  EXPECT_EQ(is_closed_and_synced, PagePredicateResult::YES);
}

// Check for a page that exists, is closed, but is not synced.
// PageIsClosedAndSynced should be false.
TEST_F(PageManagerTest, PageIsClosedAndSyncedCheckSynced) {
  bool get_page_callback_called;
  Status get_page_status;
  bool called;
  PagePredicateResult is_closed_and_synced;

  storage_->should_get_page_fail = false;
  PagePtr page;
  storage::PageIdView storage_page_id = convert::ExtendedStringView(page_id_.id);

  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(get_page_status, Status::OK);

  // Mark the page as unsynced and close it.
  storage_->set_page_storage_synced(storage_page_id, false);
  page.Unbind();
  RunLoopUntilIdle();

  Status storage_status;
  page_manager_->PageIsClosedAndSynced(
      callback::Capture(callback::SetWhenCalled(&called), &storage_status, &is_closed_and_synced));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage_status, Status::OK);
  EXPECT_EQ(is_closed_and_synced, PagePredicateResult::NO);
}

// Check for a page that exists, is closed, and synced, but was opened during
// the PageIsClosedAndSynced call. Expect an |PAGE_OPENED| result.
TEST_F(PageManagerTest, PageIsClosedAndSyncedCheckPageOpened) {
  bool get_page_callback_called;
  Status get_page_status;
  PagePredicateResult is_closed_and_synced;

  storage_->should_get_page_fail = false;
  PagePtr page;
  storage::PageIdView storage_page_id = convert::ExtendedStringView(page_id_.id);

  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(get_page_status, Status::OK);

  // Mark the page as synced and close it.
  storage_->set_page_storage_synced(storage_page_id, true);
  page.Unbind();
  RunLoopUntilIdle();

  // Call PageIsClosedAndSynced but don't let it terminate.
  bool page_is_closed_and_synced_called = false;
  storage_->DelayIsSyncedCallback(storage_page_id, true);
  Status storage_status;
  page_manager_->PageIsClosedAndSynced(
      callback::Capture(callback::SetWhenCalled(&page_is_closed_and_synced_called), &storage_status,
                        &is_closed_and_synced));
  RunLoopUntilIdle();
  EXPECT_FALSE(page_is_closed_and_synced_called);

  // Open and close the page.
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(get_page_status, Status::OK);
  page.Unbind();
  RunLoopUntilIdle();

  // Make sure PageIsClosedAndSynced terminates with a |PAGE_OPENED| result.
  storage_->CallIsSyncedCallback(storage_page_id);
  RunLoopUntilIdle();

  EXPECT_TRUE(page_is_closed_and_synced_called);
  EXPECT_EQ(storage_status, Status::OK);
  EXPECT_EQ(is_closed_and_synced, PagePredicateResult::PAGE_OPENED);
}

// Check for a page that exists, is closed, and synced. Test two concurrent
// calls to PageIsClosedAndSynced, where the second one will start and terminate
// without the page being opened by external requests.
TEST_F(PageManagerTest, PageIsClosedAndSyncedConcurrentCalls) {
  bool get_page_callback_called;
  Status get_page_status;
  storage_->should_get_page_fail = false;
  PagePtr page;
  storage::PageIdView storage_page_id = convert::ExtendedStringView(page_id_.id);

  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(get_page_status, Status::OK);

  // Mark the page as synced and close it.
  storage_->set_page_storage_synced(storage_page_id, true);
  page.Unbind();
  RunLoopUntilIdle();

  // Make a first call to PageIsClosedAndSynced but don't let it terminate.
  bool called1 = false;
  Status status1;
  PagePredicateResult is_closed_and_synced1;
  storage_->DelayIsSyncedCallback(storage_page_id, true);
  page_manager_->PageIsClosedAndSynced(
      callback::Capture(callback::SetWhenCalled(&called1), &status1, &is_closed_and_synced1));
  RunLoopUntilIdle();

  // Prepare for the second call: it will return immediately and the expected
  // result is |YES|.
  bool called2 = false;
  Status status2;
  PagePredicateResult is_closed_and_synced2;
  storage_->DelayIsSyncedCallback(storage_page_id, false);
  page_manager_->PageIsClosedAndSynced(
      callback::Capture(callback::SetWhenCalled(&called2), &status2, &is_closed_and_synced2));
  RunLoopUntilIdle();
  EXPECT_FALSE(called1);
  EXPECT_TRUE(called2);
  EXPECT_EQ(status2, Status::OK);
  EXPECT_EQ(is_closed_and_synced2, PagePredicateResult::YES);

  // Open and close the page.
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(get_page_status, Status::OK);
  page.Unbind();
  RunLoopUntilIdle();

  // Call the callback and let the first call to PageIsClosedAndSynced
  // terminate. The expected returned result is |PAGE_OPENED|.
  storage_->CallIsSyncedCallback(storage_page_id);
  RunLoopUntilIdle();

  EXPECT_TRUE(called1);
  EXPECT_EQ(status1, Status::OK);
  EXPECT_EQ(is_closed_and_synced1, PagePredicateResult::PAGE_OPENED);
}

TEST_F(PageManagerTest, PageIsClosedOfflineAndEmptyCheckNotFound) {
  bool called;
  Status status;
  PagePredicateResult is_closed_offline_empty;

  // Check for a page that doesn't exist.
  storage_->should_get_page_fail = true;
  page_manager_->PageIsClosedOfflineAndEmpty(
      callback::Capture(callback::SetWhenCalled(&called), &status, &is_closed_offline_empty));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::PAGE_NOT_FOUND);
}

TEST_F(PageManagerTest, PageIsClosedOfflineAndEmptyCheckClosed) {
  bool get_page_callback_called;
  Status get_page_status;
  bool called;
  PagePredicateResult is_closed_offline_empty;

  storage_->should_get_page_fail = false;
  PagePtr page;
  storage::PageIdView storage_page_id = convert::ExtendedStringView(page_id_.id);

  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(get_page_status, Status::OK);

  storage_->set_page_storage_offline_empty(storage_page_id, true);
  Status storage_status;
  page_manager_->PageIsClosedOfflineAndEmpty(callback::Capture(
      callback::SetWhenCalled(&called), &storage_status, &is_closed_offline_empty));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage_status, Status::OK);
  EXPECT_EQ(is_closed_offline_empty, PagePredicateResult::PAGE_OPENED);

  // Close the page. PagePredicateResult should now be true.
  page.Unbind();
  RunLoopUntilIdle();

  page_manager_->PageIsClosedOfflineAndEmpty(callback::Capture(
      callback::SetWhenCalled(&called), &storage_status, &is_closed_offline_empty));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage_status, Status::OK);
  EXPECT_EQ(is_closed_offline_empty, PagePredicateResult::YES);
}

TEST_F(PageManagerTest, PageIsClosedOfflineAndEmptyCanDeletePageOnCallback) {
  bool page_is_empty_called = false;
  Status page_is_empty_status;
  PagePredicateResult is_closed_offline_empty;
  bool delete_page_called = false;
  Status delete_page_status;

  // The page is closed, offline and empty. Try to delete the page storage in
  // the callback.
  storage_->set_page_storage_offline_empty(page_id_.id, true);
  page_manager_->PageIsClosedOfflineAndEmpty([&](Status status, PagePredicateResult result) {
    page_is_empty_called = true;
    page_is_empty_status = status;
    is_closed_offline_empty = result;

    page_manager_->DeletePageStorage(
        callback::Capture(callback::SetWhenCalled(&delete_page_called), &delete_page_status));
  });
  RunLoopUntilIdle();
  // Make sure the deletion finishes successfully.
  ASSERT_NE(nullptr, storage_->delete_page_storage_callback);
  storage_->delete_page_storage_callback(Status::OK);
  RunLoopUntilIdle();

  EXPECT_TRUE(page_is_empty_called);
  EXPECT_EQ(page_is_empty_status, Status::OK);
  EXPECT_EQ(is_closed_offline_empty, PagePredicateResult::YES);

  EXPECT_TRUE(delete_page_called);
  EXPECT_EQ(delete_page_status, Status::OK);
}

// Verifies that two successive calls to GetPage do not create 2 storages.
TEST_F(PageManagerTest, CallGetPageTwice) {
  PagePtr page1;
  bool get_page_callback_called1;
  Status get_page_status1;
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page1.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called1), &get_page_status1));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called1);
  EXPECT_EQ(get_page_status1, Status::OK);
  PagePtr page2;
  bool get_page_callback_called2;
  Status get_page_status2;
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page2.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called2), &get_page_status2));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called2);
  EXPECT_EQ(get_page_status2, Status::OK);
  EXPECT_EQ(storage_->create_page_calls.size(), 0u);
  ASSERT_EQ(storage_->get_page_calls.size(), 1u);
  EXPECT_EQ(storage_->get_page_calls[0], convert::ToString(page_id_.id));
}

TEST_F(PageManagerTest, OnExternallyUsedUnusedCalls) {
  PagePtr page1;
  bool get_page_callback_called1;
  Status get_page_status1;
  PagePtr page2;
  bool get_page_callback_called2;
  Status get_page_status2;

  EXPECT_EQ(disk_cleanup_manager_->externally_used_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->externally_unused_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->internally_used_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->internally_unused_count, 0);
  disk_cleanup_manager_->ResetCounters();

  // Open a page and check that OnExternallyUsed was called once.
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page1.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called1), &get_page_status1));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called1);
  EXPECT_EQ(get_page_status1, Status::OK);
  EXPECT_EQ(disk_cleanup_manager_->externally_used_count, 1);
  EXPECT_EQ(disk_cleanup_manager_->externally_unused_count, 0);
  // GetPage may or may not have triggered internal requests. If it did, the page must now be
  // internally unused, i.e. have the same number of OnInternallyUsed/Unused calls.
  EXPECT_EQ(disk_cleanup_manager_->internally_used_count,
            disk_cleanup_manager_->internally_unused_count);
  disk_cleanup_manager_->ResetCounters();

  // Open the page again and check that there is no new call to OnExternallyUsed.
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page2.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called2), &get_page_status2));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called2);
  EXPECT_EQ(get_page_status2, Status::OK);
  EXPECT_EQ(disk_cleanup_manager_->externally_used_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->externally_unused_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->internally_used_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->internally_unused_count, 0);
  disk_cleanup_manager_->ResetCounters();

  // Close one of the two connections and check that there is still no call to OnExternallyUnused.
  page1.Unbind();
  RunLoopUntilIdle();
  EXPECT_EQ(disk_cleanup_manager_->externally_used_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->externally_unused_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->internally_used_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->internally_unused_count, 0);
  disk_cleanup_manager_->ResetCounters();

  // Close the second connection and check that OnExternallyUnused was called once.
  page2.Unbind();
  RunLoopUntilIdle();
  EXPECT_EQ(disk_cleanup_manager_->externally_used_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->externally_unused_count, 1);
  EXPECT_EQ(disk_cleanup_manager_->internally_used_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->internally_unused_count, 0);
}

TEST_F(PageManagerTest, OnInternallyUsedUnusedCalls) {
  PagePtr page;

  EXPECT_EQ(disk_cleanup_manager_->externally_used_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->externally_unused_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->internally_used_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->internally_unused_count, 0);
  disk_cleanup_manager_->ResetCounters();

  // Make an internal request by calling PageIsClosedAndSynced.
  bool called;
  Status storage_status;
  PagePredicateResult page_state;
  page_manager_->PageIsClosedAndSynced(
      callback::Capture(callback::SetWhenCalled(&called), &storage_status, &page_state));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage_status, Status::OK);
  EXPECT_EQ(page_state, PagePredicateResult::NO);
  EXPECT_EQ(disk_cleanup_manager_->externally_used_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->externally_unused_count, 0);
  // GetPage may or may not have triggered internal requests. If it did, the page must now be
  // internally unused, i.e. have the same number of OnInternallyUsed/Unused calls.
  EXPECT_EQ(disk_cleanup_manager_->internally_used_count,
            disk_cleanup_manager_->internally_unused_count);
  disk_cleanup_manager_->ResetCounters();

  // Open the same page with an external request and check that OnExternallyUsed was called once.
  bool get_page_callback_called;
  Status get_page_status;
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(get_page_status, Status::OK);
  EXPECT_EQ(disk_cleanup_manager_->externally_used_count, 1);
  EXPECT_EQ(disk_cleanup_manager_->externally_unused_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->internally_used_count,
            disk_cleanup_manager_->internally_unused_count);
}

TEST_F(PageManagerTest, OnPageInternallyExternallyUsedUnused) {
  PagePtr page;
  storage::PageIdView storage_page_id = convert::ExtendedStringView(page_id_.id);

  EXPECT_EQ(disk_cleanup_manager_->externally_used_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->externally_unused_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->internally_used_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->internally_unused_count, 0);

  // Open and close the page through an external request.
  bool get_page_callback_called;
  Status get_page_status;
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(get_page_status, Status::OK);
  // Mark the page as synced and close it.
  storage_->set_page_storage_synced(storage_page_id, true);
  page.Unbind();
  RunLoopUntilIdle();
  EXPECT_EQ(disk_cleanup_manager_->externally_used_count, 1);
  EXPECT_EQ(disk_cleanup_manager_->externally_unused_count, 1);
  // GetPage may or may not have triggered internal requests. If it did, the page must now be
  // internally unused, i.e. have the same number of OnInternallyUsed/Unused calls.
  EXPECT_EQ(disk_cleanup_manager_->internally_used_count,
            disk_cleanup_manager_->internally_unused_count);
  disk_cleanup_manager_->ResetCounters();

  // Start an internal request but don't let it terminate.
  PagePredicateResult is_synced;
  bool page_is_synced_called = false;
  storage_->DelayIsSyncedCallback(storage_page_id, true);
  Status storage_status;
  page_manager_->PageIsClosedAndSynced(callback::Capture(
      callback::SetWhenCalled(&page_is_synced_called), &storage_status, &is_synced));
  RunLoopUntilIdle();
  EXPECT_FALSE(page_is_synced_called);
  EXPECT_EQ(disk_cleanup_manager_->externally_used_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->externally_unused_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->internally_used_count, 1);
  EXPECT_EQ(disk_cleanup_manager_->internally_unused_count, 0);
  disk_cleanup_manager_->ResetCounters();

  // Open the same page with an external request and check that OnExternallyUsed was called once.
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(get_page_status, Status::OK);
  EXPECT_EQ(disk_cleanup_manager_->externally_used_count, 1);
  EXPECT_EQ(disk_cleanup_manager_->externally_unused_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->internally_used_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->internally_unused_count, 0);
  disk_cleanup_manager_->ResetCounters();

  // Close the page. We should get the externally unused notification.
  page.Unbind();
  RunLoopUntilIdle();
  EXPECT_EQ(disk_cleanup_manager_->externally_used_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->externally_unused_count, 1);
  EXPECT_EQ(disk_cleanup_manager_->internally_used_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->internally_unused_count, 0);
  disk_cleanup_manager_->ResetCounters();

  // Terminate the internal request. We should now see the internally unused notification.
  storage_->CallIsSyncedCallback(storage_page_id);
  RunLoopUntilIdle();

  EXPECT_EQ(disk_cleanup_manager_->externally_used_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->externally_unused_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->internally_used_count, 0);
  EXPECT_EQ(disk_cleanup_manager_->internally_unused_count, 1);
}

TEST_F(PageManagerTest, DeletePageStorageWhenPageOpenFails) {
  bool get_page_callback_called;
  Status get_page_status;
  PagePtr page;
  bool called;

  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(get_page_status, Status::OK);

  // Try to delete the page while it is open. Expect to get an error.
  Status storage_status;
  page_manager_->DeletePageStorage(
      callback::Capture(callback::SetWhenCalled(&called), &storage_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage_status, Status::ILLEGAL_STATE);
}

// Verify that the PageManager opens a closed page and triggers the synchronization with the cloud
// for it.
TEST_F(PageManagerTest, StartPageSyncCheckSyncCalled) {
  bool get_page_callback_called;
  Status get_page_status;

  storage_->should_get_page_fail = false;
  PagePtr page;
  storage::PageId storage_page_id = convert::ExtendedStringView(page_id_.id).ToString();

  // Opens the page and starts the sync with the cloud for the first time.
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(get_page_status, Status::OK);
  EXPECT_EQ(sync_->GetSyncCallsCount(storage_page_id), 1);

  page.Unbind();
  RunLoopUntilIdle();

  // Reopens closed page and starts the sync.
  page_manager_->StartPageSync();
  RunLoopUntilIdle();

  EXPECT_EQ(sync_->GetSyncCallsCount(storage_page_id), 2);
}

// Verify that the PageManager does not trigger the synchronization with the cloud for the currently
// opened page.
TEST_F(PageManagerTest, StartPageSyncCheckWithOpenedPage) {
  bool get_page_callback_called;
  Status get_page_status;

  storage_->should_get_page_fail = false;
  PagePtr page;
  storage::PageId storage_page_id = convert::ExtendedStringView(page_id_.id).ToString();

  // Opens the page and starts the sync with the cloud for the first time.
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(get_page_status, Status::OK);
  EXPECT_EQ(sync_->GetSyncCallsCount(storage_page_id), 1);

  // Tries to reopen the already-opened page to start the sync.
  page_manager_->StartPageSync();
  RunLoopUntilIdle();

  EXPECT_EQ(sync_->GetSyncCallsCount(storage_page_id), 1);
}

}  // namespace
}  // namespace ledger
