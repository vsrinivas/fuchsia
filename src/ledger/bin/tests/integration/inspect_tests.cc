// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/callback/waiter.h>
#include <lib/inspect_deprecated/inspect.h>
#include <lib/inspect_deprecated/testing/inspect.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ledger/bin/inspect/inspect.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/inspect.h"
#include "src/ledger/bin/testing/ledger_matcher.h"
#include "src/ledger/bin/tests/integration/integration_test.h"
#include "src/ledger/bin/tests/integration/test_utils.h"

namespace ledger {
namespace {

using ::inspect_deprecated::testing::ChildrenMatch;
using ::testing::ElementsAre;

PageId ToPageId(const std::string& page_id_string) {
  PageId fidl_page_id;
  convert::ToArray(page_id_string, &fidl_page_id.id);
  return fidl_page_id;
}

testing::Matcher<const inspect_deprecated::ObjectHierarchy&> TopLevelMatches(
    const std::vector<testing::Matcher<const inspect_deprecated::ObjectHierarchy&>>&
        repository_matchers) {
  return ChildrenMatch(ElementsAre(RepositoriesAggregateMatches(repository_matchers)));
}

// Tests in this suite execute a series of operations and check the content of
// the Page afterwards.
class InspectTest : public IntegrationTest {
 public:
  void SetUp() override {
    IntegrationTest::SetUp();
    app_instance_ = NewLedgerAppInstance();

    ASSERT_TRUE(RunLoopUntil([this] {
      // Before we do anything interesting the app should be inspectable and have a "repositories"
      // child (that itself has no children, of course).
      inspect_deprecated::ObjectHierarchy hierarchy;
      if (app_instance_->Inspect(this, &hierarchy)) {
        return TopLevelMatches({}).Matches(hierarchy);
      }
      return false;
    }));
  }

 protected:
  std::unique_ptr<LedgerAppInstanceFactory::LedgerAppInstance> app_instance_;
};

TEST_P(InspectTest, ContentInspectableAfterDisconnection) {
  std::vector<uint8_t> ledger_name = convert::ToArray("test-ledger");
  fuchsia::ledger::PageId page_id = ToPageId("---test--page---");
  std::vector<uint8_t> key = convert::ToArray("test-key");
  std::vector<uint8_t> value = convert::ToArray("test-value");
  std::unique_ptr<CallbackWaiter> waiter;

  // Connect to a repository.
  ledger_internal::LedgerRepositoryPtr repository = app_instance_->GetTestLedgerRepository();
  waiter = NewWaiter();
  repository->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Verify that the inspection hierarchy now shows a single repository and learn from the
  // inspection the name chosen for the repository by the system under test.
  inspect_deprecated::ObjectHierarchy repository_connected_hierarchy;
  ASSERT_TRUE(app_instance_->Inspect(this, &repository_connected_hierarchy));
  ASSERT_THAT(repository_connected_hierarchy,
              TopLevelMatches({RepositoryMatches(std::nullopt, {})}));
  const inspect_deprecated::ObjectHierarchy* repositories_hierarchy =
      repository_connected_hierarchy.GetByPath({kRepositoriesInspectPathComponent.ToString()});
  ASSERT_NE(nullptr, repositories_hierarchy);
  ASSERT_EQ(1UL, repositories_hierarchy->children().size());
  const inspect_deprecated::ObjectHierarchy& repository_hierarchy =
      repositories_hierarchy->children()[0];
  const std::string& repository_display_name = repository_hierarchy.name();

  // Connect to a ledger.
  ledger::LedgerPtr ledger;
  repository->GetLedger(ledger_name, ledger.NewRequest());
  waiter = NewWaiter();
  repository->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Verify that the inspection hierarchy now shows a single ledger with the expected name.
  inspect_deprecated::ObjectHierarchy ledger_connected_hierarchy;
  ASSERT_TRUE(app_instance_->Inspect(this, &ledger_connected_hierarchy));
  EXPECT_THAT(ledger_connected_hierarchy,
              TopLevelMatches(
                  {RepositoryMatches(repository_display_name, {LedgerMatches(ledger_name, {})})}));

  // Connect to a page.
  ledger::PagePtr page;
  ledger->GetPage(std::make_unique<PageId>(page_id), page.NewRequest());
  waiter = NewWaiter();
  page->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Verify that the inspection hierarchy now shows a single page with the expected page ID.
  inspect_deprecated::ObjectHierarchy fully_connected_hierarchy;
  ASSERT_TRUE(app_instance_->Inspect(this, &fully_connected_hierarchy));
  EXPECT_THAT(fully_connected_hierarchy,
              TopLevelMatches({RepositoryMatches(
                  repository_display_name, {LedgerMatches(ledger_name, {PageMatches(page_id)})})}));

  // Mutate the page.
  page->Put(key, value);
  waiter = NewWaiter();
  page->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Verify that an inspection still shows the single page with the expected page ID.
  inspect_deprecated::ObjectHierarchy post_put_hierarchy;
  ASSERT_TRUE(app_instance_->Inspect(this, &post_put_hierarchy));
  EXPECT_THAT(post_put_hierarchy,
              TopLevelMatches({RepositoryMatches(
                  repository_display_name, {LedgerMatches(ledger_name, {PageMatches(page_id)})})}));

  // Disconnect the page and ledger bindings.
  page.Unbind();
  ledger.Unbind();
  ASSERT_TRUE(RunLoopUntil([&ledger, &page] { return !page && !ledger; }));

  // Verify that the inspection hierarchy still shows all content.
  inspect_deprecated::ObjectHierarchy page_and_ledger_unbound_hierarchy;
  ASSERT_TRUE(app_instance_->Inspect(this, &page_and_ledger_unbound_hierarchy));
  EXPECT_THAT(page_and_ledger_unbound_hierarchy,
              TopLevelMatches({RepositoryMatches(
                  repository_display_name, {LedgerMatches(ledger_name, {PageMatches(page_id)})})}));

  // Disconnect the repository binding.
  repository.Unbind();
  ASSERT_TRUE(RunLoopUntil([&repository] { return !repository; }));

  ASSERT_TRUE(RunLoopUntil([this] {
    // |LedgerRepositoryFactoryImpl| isn't an |inspect_deprecated::ChildrenManager| and doesn't have
    // the capacity to locate resident-on-disk-but-unconnected repositories, so after the repository
    // connection is dropped an inspection (eventually) shows the component with an empty
    // repositories node.
    inspect_deprecated::ObjectHierarchy hierarchy;
    if (app_instance_->Inspect(this, &hierarchy)) {
      return TopLevelMatches({}).Matches(hierarchy);
    }
    return false;
  }));
}

TEST_P(InspectTest, ConflictInCommitHistory) {
  std::vector<uint8_t> ledger_name = convert::ToArray("test-ledger");
  fuchsia::ledger::PageId page_id = ToPageId("---test--page---");
  std::vector<uint8_t> key = convert::ToArray("test-key");
  std::vector<uint8_t> value = convert::ToArray("test-value");
  std::vector<uint8_t> left_conflicting_value = convert::ToArray("left-conflicting-value");
  std::vector<uint8_t> right_conflicting_value = convert::ToArray("right-conflicting-value");
  std::unique_ptr<CallbackWaiter> waiter;

  // Connect to a repository.
  ledger_internal::LedgerRepositoryPtr repository = app_instance_->GetTestLedgerRepository();
  waiter = NewWaiter();
  repository->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Learn from an inspection the name chosen for the repository by the system under test.
  inspect_deprecated::ObjectHierarchy repository_connected_hierarchy;
  ASSERT_TRUE(app_instance_->Inspect(this, &repository_connected_hierarchy));
  ASSERT_THAT(repository_connected_hierarchy,
              TopLevelMatches({RepositoryMatches(std::nullopt, {})}));
  const inspect_deprecated::ObjectHierarchy* repositories_hierarchy =
      repository_connected_hierarchy.GetByPath({kRepositoriesInspectPathComponent.ToString()});
  ASSERT_NE(nullptr, repositories_hierarchy);
  ASSERT_EQ(1UL, repositories_hierarchy->children().size());
  const inspect_deprecated::ObjectHierarchy& repository_hierarchy =
      repositories_hierarchy->children()[0];
  const std::string& repository_display_name = repository_hierarchy.name();

  // Connect to a ledger.
  ledger::LedgerPtr ledger;
  repository->GetLedger(ledger_name, ledger.NewRequest());
  waiter = NewWaiter();
  repository->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Connect to a page.
  ledger::PagePtr page;
  ledger->GetPage(std::make_unique<PageId>(page_id), page.NewRequest());
  waiter = NewWaiter();
  page->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Verify that an inspection now shows a single page with the expected page ID.
  inspect_deprecated::ObjectHierarchy fully_connected_hierarchy;
  ASSERT_TRUE(app_instance_->Inspect(this, &fully_connected_hierarchy));
  EXPECT_THAT(fully_connected_hierarchy,
              TopLevelMatches({RepositoryMatches(
                  repository_display_name, {LedgerMatches(ledger_name, {PageMatches(page_id)})})}));

  // Mutate the page.
  page->Put(key, value);
  waiter = NewWaiter();
  page->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Verify that the inspection hierarchy still shows the single page with the expected page ID.
  inspect_deprecated::ObjectHierarchy post_put_hierarchy;
  ASSERT_TRUE(app_instance_->Inspect(this, &post_put_hierarchy));
  EXPECT_THAT(post_put_hierarchy,
              TopLevelMatches({RepositoryMatches(
                  repository_display_name, {LedgerMatches(ledger_name, {PageMatches(page_id)})})}));

  // Create a conflict on the page.
  ledger::PagePtr left_page_connection;
  ledger::PagePtr right_page_connection;
  ledger->GetPage(std::make_unique<PageId>(page_id), left_page_connection.NewRequest());
  ledger->GetPage(std::make_unique<PageId>(page_id), right_page_connection.NewRequest());
  left_page_connection->Put(key, left_conflicting_value);
  right_page_connection->Put(key, right_conflicting_value);
  waiter = NewWaiter();
  left_page_connection->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());
  waiter = NewWaiter();
  right_page_connection->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Verify that an inspection shows the single page with the expected page ID.
  inspect_deprecated::ObjectHierarchy post_conflict_hierarchy;
  ASSERT_TRUE(app_instance_->Inspect(this, &post_conflict_hierarchy));
  EXPECT_THAT(post_conflict_hierarchy,
              TopLevelMatches({RepositoryMatches(
                  repository_display_name, {LedgerMatches(ledger_name, {PageMatches(page_id)})})}));
}

INSTANTIATE_TEST_SUITE_P(InspectTest, InspectTest,
                         ::testing::ValuesIn(GetLedgerAppInstanceFactoryBuilders()));

}  // namespace
}  // namespace ledger
