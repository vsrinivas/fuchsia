// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/ledger_repository_impl.h"

#include <fuchsia/inspect/cpp/fidl.h>
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

constexpr char kInspectPathComponent[] = "test_repository";
constexpr char kObjectsName[] = "test objects";

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

// Constructs a Matcher to be matched against a top-level Inspect object (the
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
    inspect_node_ = inspect::Node(kObjectsName);

    repository_ = std::make_unique<LedgerRepositoryImpl>(
        DetachedPath(tmpfs_.root_fd()), &environment_,
        std::make_unique<storage::fake::FakeDbFactory>(dispatcher()), nullptr,
        nullptr, std::move(fake_page_eviction_manager), disk_cleanup_manager_,
        inspect_node_.CreateChild(kInspectPathComponent));
  }

  ~LedgerRepositoryImplTest() override {}

 protected:
  scoped_tmpfs::ScopedTmpFS tmpfs_;
  FakeDiskCleanupManager* disk_cleanup_manager_;
  inspect::Node inspect_node_;
  std::unique_ptr<LedgerRepositoryImpl> repository_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryImplTest);
};

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
  disk_cleanup_manager_->cleanup_callback(storage::Status::OK);
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called1);
  EXPECT_TRUE(callback_called2);
  EXPECT_EQ(Status::OK, status1);
  EXPECT_EQ(Status::OK, status2);
}

TEST_F(LedgerRepositoryImplTest, InspectAPIRequestsMetricOnMultipleBindings) {
  // When nothing has bound to the repository, check that the "requests" metric
  // is present and is zero.
  auto zeroth_hierarchy = inspect::ReadFromObject(inspect_node_);
  EXPECT_THAT(zeroth_hierarchy,
              ChildrenMatch(Contains(NodeMatches(MetricList(Contains(
                  UIntMetricIs(kRequestsInspectPathComponent, 0UL)))))));

  // When one binding has been made to the repository, check that the "requests"
  // metric is present and is one.
  ledger_internal::LedgerRepositoryPtr first_ledger_repository_ptr;
  repository_->BindRepository(first_ledger_repository_ptr.NewRequest());
  auto first_hierarchy = inspect::ReadFromObject(inspect_node_);
  EXPECT_THAT(first_hierarchy,
              ChildrenMatch(Contains(NodeMatches(MetricList(Contains(
                  UIntMetricIs(kRequestsInspectPathComponent, 1UL)))))));

  // When two bindings have been made to the repository, check that the
  // "requests" metric is present and is two.
  ledger_internal::LedgerRepositoryPtr second_ledger_repository_ptr;
  repository_->BindRepository(second_ledger_repository_ptr.NewRequest());
  auto second_hierarchy = inspect::ReadFromObject(inspect_node_);
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
  auto zeroth_hierarchy = inspect::ReadFromObject(inspect_node_);
  EXPECT_THAT(zeroth_hierarchy, HierarchyMatcher({}));

  // When one ledger has been created in the repository, check that the Inspect
  // hierarchy is as expected with a node for that one ledger.
  ledger::LedgerPtr first_ledger_ptr;
  ledger_repository_ptr->GetLedger(convert::ToArray(first_ledger_name),
                                   first_ledger_ptr.NewRequest());
  RunLoopUntilIdle();
  auto first_hierarchy = inspect::ReadFromObject(inspect_node_);
  EXPECT_THAT(first_hierarchy, HierarchyMatcher({first_ledger_name}));

  // When two ledgers have been created in the repository, check that the
  // Inspect hierarchy is as expected with nodes for both ledgers.
  ledger::LedgerPtr second_ledger_ptr;
  ledger_repository_ptr->GetLedger(convert::ToArray(second_ledger_name),
                                   second_ledger_ptr.NewRequest());
  RunLoopUntilIdle();
  auto second_hierarchy = inspect::ReadFromObject(inspect_node_);
  EXPECT_THAT(second_hierarchy,
              HierarchyMatcher({first_ledger_name, second_ledger_name}));
}

}  // namespace
}  // namespace ledger
