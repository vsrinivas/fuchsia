// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/ledger_repository_impl.h"

#include <fuchsia/inspect/cpp/fidl.h>
#include <lib/async_promise/executor.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/fit/function.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/inspect_deprecated/deprecated/expose.h>
#include <lib/inspect_deprecated/hierarchy.h>
#include <lib/inspect_deprecated/inspect.h>
#include <lib/inspect_deprecated/testing/inspect.h>

#include <vector>

#include "gtest/gtest.h"
#include "peridot/lib/convert/convert.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/ledger_repository_factory_impl.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/inspect/inspect.h"
#include "src/ledger/bin/storage/fake/fake_db_factory.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/fake_disk_cleanup_manager.h"
#include "src/ledger/bin/testing/inspect.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/string_view.h"

namespace ledger {
namespace {

constexpr char kInspectPathComponent[] = "test_repository";
constexpr char kTestTopLevelNodeName[] = "top-level-of-test node";

using ::inspect_deprecated::testing::ChildrenMatch;
using ::inspect_deprecated::testing::MetricList;
using ::inspect_deprecated::testing::NameMatches;
using ::inspect_deprecated::testing::NodeMatches;
using ::inspect_deprecated::testing::UIntMetricIs;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::IsEmpty;

// Constructs a Matcher to be matched against a test-owned Inspect object (the
// Inspect object to which the LedgerRepositoryImpl under test attaches a child)
// that validates that the matched object has a hierarchy with a node for the
// LedgerRepositoryImpl under test, a node named |kLedgersInspectPathComponent|
// under that, and a node for each of the given |ledger_names| under that.
::testing::Matcher<const inspect_deprecated::ObjectHierarchy&> HierarchyMatcher(
    const std::vector<std::string> ledger_names) {
  auto ledger_expectations =
      std::vector<::testing::Matcher<const inspect_deprecated::ObjectHierarchy&>>();
  for (const std::string& ledger_name : ledger_names) {
    ledger_expectations.push_back(NodeMatches(NameMatches(ledger_name)));
  }
  return ChildrenMatch(ElementsAre(ChildrenMatch(
      ElementsAre(AllOf(NodeMatches(NameMatches(kLedgersInspectPathComponent.ToString())),
                        ChildrenMatch(ElementsAreArray(ledger_expectations)))))));
}

class LedgerRepositoryImplTest : public TestWithEnvironment {
 public:
  LedgerRepositoryImplTest() {
    auto fake_page_eviction_manager = std::make_unique<FakeDiskCleanupManager>();
    disk_cleanup_manager_ = fake_page_eviction_manager.get();
    top_level_node_ = inspect_deprecated::Node(kTestTopLevelNodeName);
    attachment_node_ =
        top_level_node_.CreateChild(kSystemUnderTestAttachmentPointPathComponent.ToString());

    repository_ = std::make_unique<LedgerRepositoryImpl>(
        DetachedPath(tmpfs_.root_fd()), &environment_,
        std::make_unique<storage::fake::FakeDbFactory>(dispatcher()), nullptr, nullptr,
        std::move(fake_page_eviction_manager),
        std::vector<PageUsageListener*>{disk_cleanup_manager_},
        attachment_node_.CreateChild(kInspectPathComponent));
  }

  ~LedgerRepositoryImplTest() override {}

 protected:
  scoped_tmpfs::ScopedTmpFS tmpfs_;
  FakeDiskCleanupManager* disk_cleanup_manager_;
  // TODO(nathaniel): Because we use the ChildrenManager API, we need to do our
  // reads using FIDL, and because we want to use inspect_deprecated::ReadFromFidl for our
  // reads, we need to have these two objects (one parent, one child, both part
  // of the test, and with the system under test attaching to the child) rather
  // than just one. Even though this is test code this is still a layer of
  // indirection that should be eliminable in Inspect's upcoming "VMO-World".
  inspect_deprecated::Node top_level_node_;
  inspect_deprecated::Node attachment_node_;
  std::unique_ptr<LedgerRepositoryImpl> repository_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryImplTest);
};

TEST_F(LedgerRepositoryImplTest, ConcurrentCalls) {
  // Make a first call to DiskCleanUp.
  bool callback_called1 = false;
  Status status1;
  repository_->DiskCleanUp(callback::Capture(callback::SetWhenCalled(&callback_called1), &status1));

  // Make a second one before the first one has finished.
  bool callback_called2 = false;
  Status status2;
  repository_->DiskCleanUp(callback::Capture(callback::SetWhenCalled(&callback_called2), &status2));

  // Make sure both of them start running.
  RunLoopUntilIdle();

  // Both calls must wait for the cleanup manager.
  EXPECT_FALSE(callback_called1);
  EXPECT_FALSE(callback_called2);

  // Call the cleanup manager callback and expect to see an ok status for both
  // pending callbacks.
  disk_cleanup_manager_->cleanup_callback(Status::OK);
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called1);
  EXPECT_TRUE(callback_called2);
  EXPECT_EQ(status1, Status::OK);
  EXPECT_EQ(status2, Status::OK);
}

TEST_F(LedgerRepositoryImplTest, InspectAPIRequestsMetricOnMultipleBindings) {
  // When nothing has bound to the repository, check that the "requests" metric
  // is present and is zero.
  inspect_deprecated::ObjectHierarchy zeroth_hierarchy;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &zeroth_hierarchy));
  EXPECT_THAT(zeroth_hierarchy, ChildrenMatch(Contains(NodeMatches(MetricList(Contains(UIntMetricIs(
                                    kRequestsInspectPathComponent.ToString(), 0UL)))))));

  // When one binding has been made to the repository, check that the "requests"
  // metric is present and is one.
  ledger_internal::LedgerRepositoryPtr first_ledger_repository_ptr;
  repository_->BindRepository(first_ledger_repository_ptr.NewRequest());
  inspect_deprecated::ObjectHierarchy first_hierarchy;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &first_hierarchy));
  EXPECT_THAT(first_hierarchy, ChildrenMatch(Contains(NodeMatches(MetricList(Contains(UIntMetricIs(
                                   kRequestsInspectPathComponent.ToString(), 1UL)))))));

  // When two bindings have been made to the repository, check that the
  // "requests" metric is present and is two.
  ledger_internal::LedgerRepositoryPtr second_ledger_repository_ptr;
  repository_->BindRepository(second_ledger_repository_ptr.NewRequest());
  inspect_deprecated::ObjectHierarchy second_hierarchy;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &second_hierarchy));
  EXPECT_THAT(second_hierarchy, ChildrenMatch(Contains(NodeMatches(MetricList(Contains(UIntMetricIs(
                                    kRequestsInspectPathComponent.ToString(), 2UL)))))));
}

TEST_F(LedgerRepositoryImplTest, InspectAPILedgerPresence) {
  std::string first_ledger_name = "first_ledger";
  std::string second_ledger_name = "second_ledger";
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr;
  repository_->BindRepository(ledger_repository_ptr.NewRequest());

  // When nothing has requested a ledger, check that the Inspect hierarchy is as
  // expected with no nodes representing ledgers.
  inspect_deprecated::ObjectHierarchy zeroth_hierarchy;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &zeroth_hierarchy));
  EXPECT_THAT(zeroth_hierarchy, HierarchyMatcher({}));

  // When one ledger has been created in the repository, check that the Inspect
  // hierarchy is as expected with a node for that one ledger.
  ledger::LedgerPtr first_ledger_ptr;
  ledger_repository_ptr->GetLedger(convert::ToArray(first_ledger_name),
                                   first_ledger_ptr.NewRequest());
  RunLoopUntilIdle();
  inspect_deprecated::ObjectHierarchy first_hierarchy;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &first_hierarchy));
  EXPECT_THAT(first_hierarchy, HierarchyMatcher({first_ledger_name}));

  // When two ledgers have been created in the repository, check that the
  // Inspect hierarchy is as expected with nodes for both ledgers.
  ledger::LedgerPtr second_ledger_ptr;
  ledger_repository_ptr->GetLedger(convert::ToArray(second_ledger_name),
                                   second_ledger_ptr.NewRequest());
  RunLoopUntilIdle();
  inspect_deprecated::ObjectHierarchy second_hierarchy;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &second_hierarchy));
  EXPECT_THAT(second_hierarchy, HierarchyMatcher({first_ledger_name, second_ledger_name}));
}

TEST_F(LedgerRepositoryImplTest, InspectAPIDisconnectedLedgerPresence) {
  std::string first_ledger_name = "first_ledger";
  std::string second_ledger_name = "second_ledger";
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr;
  repository_->BindRepository(ledger_repository_ptr.NewRequest());

  // When nothing has yet requested a ledger, check that the Inspect hierarchy
  // is as expected with no nodes representing ledgers.
  inspect_deprecated::ObjectHierarchy zeroth_hierarchy;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &zeroth_hierarchy));
  EXPECT_THAT(zeroth_hierarchy, HierarchyMatcher({}));

  // When one ledger has been created in the repository, check that the Inspect
  // hierarchy is as expected with a node for that one ledger.
  ledger::LedgerPtr first_ledger_ptr;
  ledger_repository_ptr->GetLedger(convert::ToArray(first_ledger_name),
                                   first_ledger_ptr.NewRequest());
  RunLoopUntilIdle();
  inspect_deprecated::ObjectHierarchy hierarchy_after_one_connection;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &hierarchy_after_one_connection));
  EXPECT_THAT(hierarchy_after_one_connection, HierarchyMatcher({first_ledger_name}));

  // When two ledgers have been created in the repository, check that the
  // Inspect hierarchy is as expected with nodes for both ledgers.
  ledger::LedgerPtr second_ledger_ptr;
  ledger_repository_ptr->GetLedger(convert::ToArray(second_ledger_name),
                                   second_ledger_ptr.NewRequest());
  RunLoopUntilIdle();
  inspect_deprecated::ObjectHierarchy hierarchy_after_two_connections;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &hierarchy_after_two_connections));
  EXPECT_THAT(hierarchy_after_two_connections,
              HierarchyMatcher({first_ledger_name, second_ledger_name}));

  first_ledger_ptr.Unbind();
  RunLoopUntilIdle();

  // When one of the two ledgers has been disconnected, check that an inspection
  // still finds both.
  inspect_deprecated::ObjectHierarchy hierarchy_after_one_disconnection;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &hierarchy_after_one_disconnection));
  EXPECT_THAT(hierarchy_after_one_disconnection,
              HierarchyMatcher({first_ledger_name, second_ledger_name}));

  second_ledger_ptr.Unbind();
  RunLoopUntilIdle();

  // When both of the ledgers have been disconnected, check that an inspection
  // still finds both.
  inspect_deprecated::ObjectHierarchy hierarchy_after_two_disconnections;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &hierarchy_after_two_disconnections));
  EXPECT_THAT(hierarchy_after_two_disconnections,
              HierarchyMatcher({first_ledger_name, second_ledger_name}));
}

// Verifies that closing a ledger repository closes the LedgerRepository
// connections once all Ledger connections are themselves closed.
TEST_F(LedgerRepositoryImplTest, Close) {
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr1;
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr2;
  ledger::LedgerPtr ledger_ptr;

  repository_->BindRepository(ledger_repository_ptr1.NewRequest());
  repository_->BindRepository(ledger_repository_ptr2.NewRequest());

  bool on_empty_called;
  repository_->set_on_empty(callback::SetWhenCalled(&on_empty_called));

  bool ptr1_closed;
  zx_status_t ptr1_closed_status;
  ledger_repository_ptr1.set_error_handler(
      callback::Capture(callback::SetWhenCalled(&ptr1_closed), &ptr1_closed_status));
  bool ptr2_closed;
  zx_status_t ptr2_closed_status;
  ledger_repository_ptr2.set_error_handler(
      callback::Capture(callback::SetWhenCalled(&ptr2_closed), &ptr2_closed_status));
  bool ledger_closed;
  zx_status_t ledger_closed_status;
  ledger_ptr.set_error_handler(
      callback::Capture(callback::SetWhenCalled(&ledger_closed), &ledger_closed_status));

  ledger_repository_ptr1->GetLedger(convert::ToArray("ledger"), ledger_ptr.NewRequest());
  RunLoopUntilIdle();
  EXPECT_FALSE(on_empty_called);
  EXPECT_FALSE(ptr1_closed);
  EXPECT_FALSE(ptr2_closed);
  EXPECT_FALSE(ledger_closed);

  ledger_repository_ptr2->Close();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_empty_called);
  EXPECT_FALSE(ptr1_closed);
  EXPECT_FALSE(ptr2_closed);
  EXPECT_FALSE(ledger_closed);

  ledger_ptr.Unbind();
  RunLoopUntilIdle();

  EXPECT_TRUE(on_empty_called);
  EXPECT_FALSE(ptr1_closed);
  EXPECT_FALSE(ptr2_closed);

  // Delete the repository, as it would be done by LedgerRepositoryFactory when
  // the |on_empty| callback is called.
  repository_.reset();
  RunLoopUntilIdle();
  EXPECT_TRUE(ptr1_closed);
  EXPECT_TRUE(ptr2_closed);

  EXPECT_EQ(ptr1_closed_status, ZX_OK);
  EXPECT_EQ(ptr2_closed_status, ZX_OK);
}

}  // namespace
}  // namespace ledger
