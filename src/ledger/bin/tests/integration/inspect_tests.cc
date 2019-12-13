// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ledger/internal/cpp/fidl.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ledger/bin/inspect/inspect.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/inspect.h"
#include "src/ledger/bin/testing/ledger_app_instance_factory.h"
#include "src/ledger/bin/testing/ledger_matcher.h"
#include "src/ledger/bin/tests/integration/integration_test.h"
#include "src/ledger/bin/tests/integration/test_page_watcher.h"
#include "src/ledger/bin/tests/integration/test_utils.h"
#include "src/ledger/lib/callback/waiter.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/inspect_deprecated/inspect.h"
#include "src/lib/inspect_deprecated/testing/inspect.h"

namespace ledger {
namespace {

using ::inspect_deprecated::testing::ChildrenMatch;
using ::testing::_;
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
      repository_connected_hierarchy.GetByPath(
          {convert::ToString(kRepositoriesInspectPathComponent)});
  ASSERT_NE(nullptr, repositories_hierarchy);
  ASSERT_EQ(1UL, repositories_hierarchy->children().size());
  const inspect_deprecated::ObjectHierarchy& repository_hierarchy =
      repositories_hierarchy->children()[0];
  const std::string& repository_display_name = repository_hierarchy.name();

  // Connect to a ledger.
  LedgerPtr ledger;
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
  PagePtr page;
  ledger->GetPage(std::make_unique<PageId>(page_id), page.NewRequest());
  waiter = NewWaiter();
  page->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Verify that the inspection hierarchy now shows a single page with the expected page ID and with
  // the root commit ID as its head.
  inspect_deprecated::ObjectHierarchy fully_connected_hierarchy;
  ASSERT_TRUE(app_instance_->Inspect(this, &fully_connected_hierarchy));
  EXPECT_THAT(
      fully_connected_hierarchy,
      TopLevelMatches({RepositoryMatches(
          repository_display_name,
          {LedgerMatches(ledger_name,
                         {PageMatches(page_id.id, {convert::ToString(storage::kFirstPageCommitId)},
                                      {CommitMatches(convert::ToString(storage::kFirstPageCommitId),
                                                     {}, {})})})})}));

  // Mutate the page.
  page->Put(key, value);
  waiter = NewWaiter();
  page->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Verify that an inspection still shows the single page with the expected page ID and learn from
  // the inspection the commit ID of the new head of the page.
  inspect_deprecated::ObjectHierarchy post_put_hierarchy;
  ASSERT_TRUE(app_instance_->Inspect(this, &post_put_hierarchy));
  EXPECT_THAT(
      post_put_hierarchy,
      TopLevelMatches({RepositoryMatches(
          repository_display_name,
          {LedgerMatches(
              ledger_name,
              {PageMatches(
                  page_id.id, {std::nullopt},
                  {CommitMatches(convert::ToString(storage::kFirstPageCommitId), {}, {}),
                   CommitMatches(std::nullopt, {convert::ToString(storage::kFirstPageCommitId)},
                                 {{{key.begin(), key.end()}, {value}}})})})})}));
  const inspect_deprecated::ObjectHierarchy* post_put_heads_node = post_put_hierarchy.GetByPath(
      {convert::ToString(kRepositoriesInspectPathComponent), repository_display_name,
       convert::ToString(kLedgersInspectPathComponent), convert::ToString(ledger_name),
       convert::ToString(kPagesInspectPathComponent),
       PageIdToDisplayName(convert::ToString(page_id.id)),
       convert::ToString(kHeadsInspectPathComponent)});
  ASSERT_EQ(1UL, post_put_heads_node->children().size());
  const inspect_deprecated::ObjectHierarchy& post_put_head_node =
      post_put_heads_node->children()[0];
  storage::CommitId post_put_head;
  ASSERT_TRUE(CommitDisplayNameToCommitId(post_put_head_node.name(), &post_put_head));

  // Disconnect the page and ledger bindings.
  page.Unbind();
  ledger.Unbind();
  ASSERT_TRUE(RunLoopUntil([&ledger, &page] { return !page && !ledger; }));

  // Verify that the inspection hierarchy still shows all content.
  inspect_deprecated::ObjectHierarchy page_and_ledger_unbound_hierarchy;
  ASSERT_TRUE(app_instance_->Inspect(this, &page_and_ledger_unbound_hierarchy));
  EXPECT_THAT(
      page_and_ledger_unbound_hierarchy,
      TopLevelMatches({RepositoryMatches(
          repository_display_name,
          {LedgerMatches(
              ledger_name,
              {PageMatches(
                  page_id.id, {post_put_head},
                  {CommitMatches(convert::ToString(storage::kFirstPageCommitId), {}, {}),
                   CommitMatches(post_put_head, {convert::ToString(storage::kFirstPageCommitId)},
                                 {{{key.begin(), key.end()}, {value}}})})})})}));

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
      repository_connected_hierarchy.GetByPath(
          {convert::ToString(kRepositoriesInspectPathComponent)});
  ASSERT_NE(nullptr, repositories_hierarchy);
  ASSERT_EQ(1UL, repositories_hierarchy->children().size());
  const inspect_deprecated::ObjectHierarchy& repository_hierarchy =
      repositories_hierarchy->children()[0];
  const std::string& repository_display_name = repository_hierarchy.name();

  // Connect to a ledger.
  LedgerPtr ledger;
  repository->GetLedger(ledger_name, ledger.NewRequest());
  waiter = NewWaiter();
  repository->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Connect to a page.
  PagePtr page;
  ledger->GetPage(std::make_unique<PageId>(page_id), page.NewRequest());
  waiter = NewWaiter();
  page->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Verify that an inspection now shows a single page with the expected page ID and with the root
  // commit ID as its head.
  inspect_deprecated::ObjectHierarchy fully_connected_hierarchy;
  ASSERT_TRUE(app_instance_->Inspect(this, &fully_connected_hierarchy));
  EXPECT_THAT(
      fully_connected_hierarchy,
      TopLevelMatches({RepositoryMatches(
          repository_display_name,
          {LedgerMatches(ledger_name,
                         {PageMatches(page_id.id, {convert::ToString(storage::kFirstPageCommitId)},
                                      {CommitMatches(convert::ToString(storage::kFirstPageCommitId),
                                                     {}, {})})})})}));

  // Mutate the page.
  page->Put(key, value);
  // Get a snapshot of the page. This prevents all commit pruning.
  PageSnapshotPtr page_snapshot_ptr;
  // Create a watcher on the page snapshot so that visible change are monitored.
  auto snpashot_waiter = NewWaiter();
  PageWatcherPtr watcher_ptr;
  TestPageWatcher watcher(watcher_ptr.NewRequest(), snpashot_waiter->GetCallback());
  page->GetSnapshot(page_snapshot_ptr.NewRequest(), {}, std::move(watcher_ptr));
  waiter = NewWaiter();
  page->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Verify that the inspection hierarchy still shows the single page with the expected page ID and
  // learn from the inspection the commit ID of the new head of the page.
  inspect_deprecated::ObjectHierarchy post_put_hierarchy;
  ASSERT_TRUE(app_instance_->Inspect(this, &post_put_hierarchy));
  EXPECT_THAT(
      post_put_hierarchy,
      TopLevelMatches({RepositoryMatches(
          repository_display_name,
          {LedgerMatches(
              ledger_name,
              {PageMatches(
                  page_id.id, {std::nullopt},
                  {CommitMatches(convert::ToString(storage::kFirstPageCommitId), {}, {}),
                   CommitMatches(std::nullopt, {convert::ToString(storage::kFirstPageCommitId)},
                                 {{{key.begin(), key.end()}, {value}}})})})})}));
  const inspect_deprecated::ObjectHierarchy* post_put_heads_node = post_put_hierarchy.GetByPath(
      {convert::ToString(kRepositoriesInspectPathComponent), repository_display_name,
       convert::ToString(kLedgersInspectPathComponent), convert::ToString(ledger_name),
       convert::ToString(kPagesInspectPathComponent),
       PageIdToDisplayName(convert::ToString(page_id.id)),
       convert::ToString(kHeadsInspectPathComponent)});
  ASSERT_EQ(1UL, post_put_heads_node->children().size());
  const inspect_deprecated::ObjectHierarchy& post_put_head_node =
      post_put_heads_node->children()[0];
  storage::CommitId post_put_head_id;
  ASSERT_TRUE(CommitDisplayNameToCommitId(post_put_head_node.name(), &post_put_head_id));

  // Create a conflict on the page.
  PagePtr left_page_connection;
  PagePtr right_page_connection;
  ledger->GetPage(std::make_unique<PageId>(page_id), left_page_connection.NewRequest());
  ledger->GetPage(std::make_unique<PageId>(page_id), right_page_connection.NewRequest());
  // Start transactions to ensure that mutation are concurrent.
  left_page_connection->StartTransaction();
  right_page_connection->StartTransaction();
  waiter = NewWaiter();
  left_page_connection->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());
  waiter = NewWaiter();
  right_page_connection->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());
  // Both pages are now on conflicting transactions. Mutate the same key on
  // both, commit and sync to ensure the ledger is in a cohrent, known state.
  left_page_connection->Put(key, left_conflicting_value);
  left_page_connection->Commit();

  // Wait for the first change to be visible on the initial page.
  ASSERT_TRUE(snpashot_waiter->RunUntilCalled());

  // Commit the change on the second connection.
  right_page_connection->Put(key, right_conflicting_value);
  right_page_connection->Commit();

  // Wait for a new change to be visible on the initial page. This will be the
  // merged change.
  ASSERT_TRUE(snpashot_waiter->RunUntilCalled());

  // Verify that an inspection still shows the single page with the expected page ID, learn from
  // the inspection the commit ID of the new, post-conflict head of the page, and then verify that
  // the entire hierarchy is as expected.
  inspect_deprecated::ObjectHierarchy post_conflict_hierarchy;
  ASSERT_TRUE(app_instance_->Inspect(this, &post_conflict_hierarchy));
  EXPECT_THAT(post_conflict_hierarchy,
              TopLevelMatches({RepositoryMatches(
                  repository_display_name,
                  {LedgerMatches(
                      ledger_name,
                      {PageMatches(
                          page_id.id, {std::nullopt},
                          {
                              CommitMatches(convert::ToString(storage::kFirstPageCommitId), {}, {}),
                              CommitMatches(post_put_head_id,
                                            {convert::ToString(storage::kFirstPageCommitId)},
                                            {{{key.begin(), key.end()}, {value}}}),
                              CommitMatches(std::nullopt, {post_put_head_id},
                                            {{{key.begin(), key.end()},
                                              {left_conflicting_value, right_conflicting_value}}}),
                              CommitMatches(std::nullopt, {post_put_head_id},
                                            {{{key.begin(), key.end()},
                                              {left_conflicting_value, right_conflicting_value}}}),
                              _,
                          })})})}));
  const inspect_deprecated::ObjectHierarchy* post_conflict_heads_node =
      post_conflict_hierarchy.GetByPath(
          {convert::ToString(kRepositoriesInspectPathComponent), repository_display_name,
           convert::ToString(kLedgersInspectPathComponent), convert::ToString(ledger_name),
           convert::ToString(kPagesInspectPathComponent),
           PageIdToDisplayName(convert::ToString(page_id.id)),
           convert::ToString(kHeadsInspectPathComponent)});
  ASSERT_EQ(1UL, post_conflict_heads_node->children().size());
  const inspect_deprecated::ObjectHierarchy& post_conflict_head_node =
      post_conflict_heads_node->children()[0];
  storage::CommitId post_conflict_head_id;
  ASSERT_TRUE(CommitDisplayNameToCommitId(post_conflict_head_node.name(), &post_conflict_head_id));
  EXPECT_NE(post_put_head_id, post_conflict_head_id);
  const inspect_deprecated::ObjectHierarchy* post_conflict_head_commit_parents_node =
      post_conflict_hierarchy.GetByPath({
          convert::ToString(kRepositoriesInspectPathComponent),
          repository_display_name,
          convert::ToString(kLedgersInspectPathComponent),
          convert::ToString(ledger_name),
          convert::ToString(kPagesInspectPathComponent),
          PageIdToDisplayName(convert::ToString(page_id.id)),
          convert::ToString(kCommitsInspectPathComponent),
          CommitIdToDisplayName(post_conflict_head_id),
          convert::ToString(kParentsInspectPathComponent),
      });
  ASSERT_EQ(post_conflict_head_commit_parents_node->children().size(), 2UL);
  // Arbitrarily assign the IDs of the conflicting commits as "first" and "second"; note that it is
  // not guaranteed which of |left_connection| and |right_connection| created which conflicting
  // commit.
  storage::CommitId first_conflicting_commit_id;
  storage::CommitId second_conflicting_commit_id;
  ASSERT_TRUE(CommitDisplayNameToCommitId(
      post_conflict_head_commit_parents_node->children()[0].name(), &first_conflicting_commit_id));
  ASSERT_TRUE(CommitDisplayNameToCommitId(
      post_conflict_head_commit_parents_node->children()[1].name(), &second_conflicting_commit_id));
  // Determine whether or not |left_conflicting_value| was made in the commit with ID
  // |first_conflicting_commit_id|.
  const inspect_deprecated::ObjectHierarchy* post_conflict_first_conflicting_commit_entry_node =
      post_conflict_hierarchy.GetByPath({
          convert::ToString(kRepositoriesInspectPathComponent),
          repository_display_name,
          convert::ToString(kLedgersInspectPathComponent),
          convert::ToString(ledger_name),
          convert::ToString(kPagesInspectPathComponent),
          PageIdToDisplayName(convert::ToString(page_id.id)),
          convert::ToString(kCommitsInspectPathComponent),
          post_conflict_head_commit_parents_node->children()[0].name(),
          convert::ToString(kEntriesInspectPathComponent),
          KeyToDisplayName({key.begin(), key.end()}),
      });
  bool left_was_first =
      left_conflicting_value == post_conflict_first_conflicting_commit_entry_node->node()
                                    .properties()[0]
                                    .Get<inspect_deprecated::hierarchy::ByteVectorProperty>()
                                    .value();
  EXPECT_THAT(
      post_conflict_hierarchy,
      TopLevelMatches({RepositoryMatches(
          repository_display_name,
          {LedgerMatches(
              ledger_name,
              {PageMatches(
                  page_id.id, {std::nullopt},
                  {
                      CommitMatches(convert::ToString(storage::kFirstPageCommitId), {}, {}),
                      CommitMatches(post_put_head_id,
                                    {convert::ToString(storage::kFirstPageCommitId)},
                                    {{{key.begin(), key.end()}, {value}}}),
                      CommitMatches(
                          first_conflicting_commit_id, {post_put_head_id},
                          {{{key.begin(), key.end()},
                            {left_was_first ? left_conflicting_value : right_conflicting_value}}}),
                      CommitMatches(
                          second_conflicting_commit_id, {post_put_head_id},
                          {{{key.begin(), key.end()},
                            {left_was_first ? right_conflicting_value : left_conflicting_value}}}),
                      CommitMatches(post_conflict_head_id,
                                    {first_conflicting_commit_id, second_conflicting_commit_id},
                                    {{{key.begin(), key.end()},
                                      {left_conflicting_value, right_conflicting_value}}}),
                  })})})}));
}

// Disable cloud synchronization for Inspect tests to avoid garbage-collection of synchronized
// content, which would make it disappear before it can be inspected.
INSTANTIATE_TEST_SUITE_P(
    InspectTest, InspectTest,
    ::testing::ValuesIn(GetLedgerAppInstanceFactoryBuilders(EnableSynchronization::OFFLINE_ONLY)),
    PrintLedgerAppInstanceFactoryBuilder());

}  // namespace
}  // namespace ledger
