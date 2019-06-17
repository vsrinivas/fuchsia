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
#include <lib/inspect/deprecated/expose.h>
#include <lib/inspect/hierarchy.h>
#include <lib/inspect/inspect.h>
#include <lib/inspect/reader.h>
#include <lib/inspect/testing/inspect.h>

#include <vector>

#include "gtest/gtest.h"
#include "peridot/lib/convert/convert.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/ledger_repository_factory_impl.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/fake/fake_db_factory.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/fake_disk_cleanup_manager.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/string_view.h"

namespace ledger {
namespace {

constexpr char kSystemUnderTestAttachmentPointPathComponent[] =
    "attachment_point";
constexpr char kInspectPathComponent[] = "test_repository";
constexpr char kTestTopLevelNodeName[] = "top-level-of-test node";

using ::inspect::testing::ChildrenMatch;
using ::inspect::testing::MetricList;
using ::inspect::testing::NameMatches;
using ::inspect::testing::NodeMatches;
using ::inspect::testing::UIntMetricIs;
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
::testing::Matcher<const inspect::ObjectHierarchy&> HierarchyMatcher(
    const std::vector<std::string> ledger_names) {
  auto ledger_expectations =
      std::vector<::testing::Matcher<const inspect::ObjectHierarchy&>>();
  for (const std::string& ledger_name : ledger_names) {
    ledger_expectations.push_back(NodeMatches(NameMatches(ledger_name)));
  }
  return ChildrenMatch(ElementsAre(ChildrenMatch(ElementsAre(
      AllOf(NodeMatches(NameMatches(kLedgersInspectPathComponent)),
            ChildrenMatch(ElementsAreArray(ledger_expectations)))))));
}

class LedgerRepositoryImplTest : public TestWithEnvironment {
 public:
  LedgerRepositoryImplTest() {
    auto fake_page_eviction_manager =
        std::make_unique<FakeDiskCleanupManager>();
    disk_cleanup_manager_ = fake_page_eviction_manager.get();
    top_level_node_ = inspect::Node(kTestTopLevelNodeName);
    attachment_node_ = top_level_node_.CreateChild(
        kSystemUnderTestAttachmentPointPathComponent);

    repository_ = std::make_unique<LedgerRepositoryImpl>(
        DetachedPath(tmpfs_.root_fd()), &environment_,
        std::make_unique<storage::fake::FakeDbFactory>(dispatcher()), nullptr,
        nullptr, std::move(fake_page_eviction_manager), disk_cleanup_manager_,
        attachment_node_.CreateChild(kInspectPathComponent));
  }

  ~LedgerRepositoryImplTest() override {}

 protected:
  testing::AssertionResult Read(inspect::ObjectHierarchy* hierarchy);

  scoped_tmpfs::ScopedTmpFS tmpfs_;
  FakeDiskCleanupManager* disk_cleanup_manager_;
  // TODO(nathaniel): Because we use the ChildrenManager API, we need to do our
  // reads using FIDL, and because we want to use inspect::ReadFromFidl for our
  // reads, we need to have these two objects (one parent, one child, both part
  // of the test, and with the system under test attaching to the child) rather
  // than just one. Even though this is test code this is still a layer of
  // indirection that should be eliminable in Inspect's upcoming "VMO-World".
  inspect::Node top_level_node_;
  inspect::Node attachment_node_;
  std::unique_ptr<LedgerRepositoryImpl> repository_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryImplTest);
};

testing::AssertionResult LedgerRepositoryImplTest::Read(
    inspect::ObjectHierarchy* hierarchy) {
  bool callback_called;
  bool success;
  fidl::InterfaceHandle<fuchsia::inspect::Inspect> inspect_handle;
  top_level_node_.object_dir().object()->OpenChild(
      kSystemUnderTestAttachmentPointPathComponent, inspect_handle.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&callback_called), &success));
  RunLoopUntilIdle();
  if (!callback_called) {
    return ::testing::AssertionFailure()
           << "Callback passed to OpenChild not called!";
  }
  if (!success) {
    return ::testing::AssertionFailure() << "OpenChild not successful!";
  }

  callback_called = false;
  async::Executor executor(dispatcher());
  fit::result<inspect::ObjectHierarchy> hierarchy_result;
  auto hierarchy_promise =
      inspect::ReadFromFidl(inspect::ObjectReader(std::move(inspect_handle)))
          .then([&](fit::result<inspect::ObjectHierarchy>&
                        then_hierarchy_result) {
            callback_called = true;
            hierarchy_result = std::move(then_hierarchy_result);
          });
  executor.schedule_task(std::move(hierarchy_promise));
  RunLoopUntilIdle();
  if (!callback_called) {
    return ::testing::AssertionFailure()
           << "Callback passed to ReadFromFidl(<...>).then not called!";
  } else if (!hierarchy_result.is_ok()) {
    return ::testing::AssertionFailure() << "Hierarchy result not okay!";
  }
  *hierarchy = hierarchy_result.take_value();
  return ::testing::AssertionSuccess();
}

TEST_F(LedgerRepositoryImplTest, ConcurrentCalls) {
  // Make a first call to DiskCleanUp.
  bool callback_called1 = false;
  Status status1;
  repository_->DiskCleanUp(
      callback::Capture(callback::SetWhenCalled(&callback_called1), &status1));

  // Make a second one before the first one has finished.
  bool callback_called2 = false;
  Status status2;
  repository_->DiskCleanUp(
      callback::Capture(callback::SetWhenCalled(&callback_called2), &status2));

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
  EXPECT_EQ(Status::OK, status1);
  EXPECT_EQ(Status::OK, status2);
}

TEST_F(LedgerRepositoryImplTest, InspectAPIRequestsMetricOnMultipleBindings) {
  // When nothing has bound to the repository, check that the "requests" metric
  // is present and is zero.
  inspect::ObjectHierarchy zeroth_hierarchy;
  ASSERT_TRUE(Read(&zeroth_hierarchy));
  EXPECT_THAT(zeroth_hierarchy,
              ChildrenMatch(Contains(NodeMatches(MetricList(Contains(
                  UIntMetricIs(kRequestsInspectPathComponent, 0UL)))))));

  // When one binding has been made to the repository, check that the "requests"
  // metric is present and is one.
  ledger_internal::LedgerRepositoryPtr first_ledger_repository_ptr;
  repository_->BindRepository(first_ledger_repository_ptr.NewRequest());
  inspect::ObjectHierarchy first_hierarchy;
  ASSERT_TRUE(Read(&first_hierarchy));
  EXPECT_THAT(first_hierarchy,
              ChildrenMatch(Contains(NodeMatches(MetricList(Contains(
                  UIntMetricIs(kRequestsInspectPathComponent, 1UL)))))));

  // When two bindings have been made to the repository, check that the
  // "requests" metric is present and is two.
  ledger_internal::LedgerRepositoryPtr second_ledger_repository_ptr;
  repository_->BindRepository(second_ledger_repository_ptr.NewRequest());
  inspect::ObjectHierarchy second_hierarchy;
  ASSERT_TRUE(Read(&second_hierarchy));
  EXPECT_THAT(second_hierarchy,
              ChildrenMatch(Contains(NodeMatches(MetricList(Contains(
                  UIntMetricIs(kRequestsInspectPathComponent, 2UL)))))));
}

TEST_F(LedgerRepositoryImplTest, InspectAPILedgerPresence) {
  std::string first_ledger_name = "first_ledger";
  std::string second_ledger_name = "second_ledger";
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr;
  repository_->BindRepository(ledger_repository_ptr.NewRequest());

  // When nothing has requested a ledger, check that the Inspect hierarchy is as
  // expected with no nodes representing ledgers.
  inspect::ObjectHierarchy zeroth_hierarchy;
  ASSERT_TRUE(Read(&zeroth_hierarchy));
  EXPECT_THAT(zeroth_hierarchy, HierarchyMatcher({}));

  // When one ledger has been created in the repository, check that the Inspect
  // hierarchy is as expected with a node for that one ledger.
  ledger::LedgerPtr first_ledger_ptr;
  ledger_repository_ptr->GetLedger(convert::ToArray(first_ledger_name),
                                   first_ledger_ptr.NewRequest());
  RunLoopUntilIdle();
  inspect::ObjectHierarchy first_hierarchy;
  ASSERT_TRUE(Read(&first_hierarchy));
  EXPECT_THAT(first_hierarchy, HierarchyMatcher({first_ledger_name}));

  // When two ledgers have been created in the repository, check that the
  // Inspect hierarchy is as expected with nodes for both ledgers.
  ledger::LedgerPtr second_ledger_ptr;
  ledger_repository_ptr->GetLedger(convert::ToArray(second_ledger_name),
                                   second_ledger_ptr.NewRequest());
  RunLoopUntilIdle();
  inspect::ObjectHierarchy second_hierarchy;
  ASSERT_TRUE(Read(&second_hierarchy));
  EXPECT_THAT(second_hierarchy,
              HierarchyMatcher({first_ledger_name, second_ledger_name}));
}

TEST_F(LedgerRepositoryImplTest, InspectAPIDisconnectedLedgerPresence) {
  std::string first_ledger_name = "first_ledger";
  std::string second_ledger_name = "second_ledger";
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr;
  repository_->BindRepository(ledger_repository_ptr.NewRequest());

  // When nothing has yet requested a ledger, check that the Inspect hierarchy
  // is as expected with no nodes representing ledgers.
  inspect::ObjectHierarchy zeroth_hierarchy;
  ASSERT_TRUE(Read(&zeroth_hierarchy));
  EXPECT_THAT(zeroth_hierarchy, HierarchyMatcher({}));

  // When one ledger has been created in the repository, check that the Inspect
  // hierarchy is as expected with a node for that one ledger.
  ledger::LedgerPtr first_ledger_ptr;
  ledger_repository_ptr->GetLedger(convert::ToArray(first_ledger_name),
                                   first_ledger_ptr.NewRequest());
  RunLoopUntilIdle();
  inspect::ObjectHierarchy hierarchy_after_one_connection;
  ASSERT_TRUE(Read(&hierarchy_after_one_connection));
  EXPECT_THAT(hierarchy_after_one_connection,
              HierarchyMatcher({first_ledger_name}));

  // When two ledgers have been created in the repository, check that the
  // Inspect hierarchy is as expected with nodes for both ledgers.
  ledger::LedgerPtr second_ledger_ptr;
  ledger_repository_ptr->GetLedger(convert::ToArray(second_ledger_name),
                                   second_ledger_ptr.NewRequest());
  RunLoopUntilIdle();
  inspect::ObjectHierarchy hierarchy_after_two_connections;
  ASSERT_TRUE(Read(&hierarchy_after_two_connections));
  EXPECT_THAT(hierarchy_after_two_connections,
              HierarchyMatcher({first_ledger_name, second_ledger_name}));

  first_ledger_ptr.Unbind();
  RunLoopUntilIdle();

  // When one of the two ledgers has been disconnected, check that an inspection
  // still finds both.
  inspect::ObjectHierarchy hierarchy_after_one_disconnection;
  ASSERT_TRUE(Read(&hierarchy_after_one_disconnection));
  EXPECT_THAT(hierarchy_after_one_disconnection,
              HierarchyMatcher({first_ledger_name, second_ledger_name}));

  second_ledger_ptr.Unbind();
  RunLoopUntilIdle();

  // When both of the ledgers have been disconnected, check that an inspection
  // still finds both.
  inspect::ObjectHierarchy hierarchy_after_two_disconnections;
  ASSERT_TRUE(Read(&hierarchy_after_two_disconnections));
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
  ledger_repository_ptr1.set_error_handler(callback::Capture(
      callback::SetWhenCalled(&ptr1_closed), &ptr1_closed_status));
  bool ptr2_closed;
  zx_status_t ptr2_closed_status;
  ledger_repository_ptr2.set_error_handler(callback::Capture(
      callback::SetWhenCalled(&ptr2_closed), &ptr2_closed_status));
  bool ledger_closed;
  zx_status_t ledger_closed_status;
  ledger_ptr.set_error_handler(callback::Capture(
      callback::SetWhenCalled(&ledger_closed), &ledger_closed_status));

  ledger_repository_ptr1->GetLedger(convert::ToArray("ledger"),
                                    ledger_ptr.NewRequest());
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

  EXPECT_EQ(ZX_OK, ptr1_closed_status);
  EXPECT_EQ(ZX_OK, ptr2_closed_status);
}

}  // namespace
}  // namespace ledger
