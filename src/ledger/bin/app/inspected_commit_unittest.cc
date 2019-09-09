// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/inspected_commit.h"

#include <lib/callback/set_when_called.h>
#include <lib/fit/function.h>
#include <lib/inspect_deprecated/inspect.h>

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/inspect/inspect.h"
#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/storage/testing/id_and_parent_ids_commit.h"
#include "src/ledger/bin/testing/inspect.h"
#include "src/ledger/bin/testing/test_with_environment.h"

namespace ledger {
namespace {

constexpr fxl::StringView kTestTopLevelNodeName = "top-level-of-test node";

class InspectedCommitTest : public TestWithEnvironment {
 public:
  InspectedCommitTest() = default;
  ~InspectedCommitTest() override = default;

  // gtest::TestWithEnvironment:
  void SetUp() override {
    top_level_node_ = inspect_deprecated::Node(kTestTopLevelNodeName.ToString());
    attachment_node_ =
        top_level_node_.CreateChild(kSystemUnderTestAttachmentPointPathComponent.ToString());
  }

 protected:
  // TODO(nathaniel): Because we use the ChildrenManager API, we need to do our reads using FIDL,
  // and because we want to use inspect::ReadFromFidl for our reads, we need to have these two
  // objects (one parent, one child, both part of the test, and with the system under test attaching
  // to the child) rather than just one. Even though this is test code this is still a layer of
  // indirection that should be eliminable in Inspect's upcoming "VMO-World".
  inspect_deprecated::Node top_level_node_;
  inspect_deprecated::Node attachment_node_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(InspectedCommitTest);
};

TEST_F(InspectedCommitTest, OnEmptyCalled) {
  std::set<storage::CommitId> parents = {
      storage::CommitId("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\x01",
                        storage::kCommitIdSize),
      storage::CommitId("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\x02",
                        storage::kCommitIdSize),
      storage::CommitId("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\x03",
                        storage::kCommitIdSize),
      storage::CommitId("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\x04",
                        storage::kCommitIdSize),
  };
  storage::CommitId id = storage::CommitId(
      "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\x05", storage::kCommitIdSize);
  bool on_empty_called;

  std::unique_ptr<storage::IdAndParentIdsCommit> commit =
      std::make_unique<storage::IdAndParentIdsCommit>(id, parents);
  inspect_deprecated::Node commit_node = attachment_node_.CreateChild(CommitIdToDisplayName(id));
  InspectedCommit inspected_commit = InspectedCommit(std::move(commit_node), std::move(commit));

  inspected_commit.set_on_empty(callback::SetWhenCalled(&on_empty_called));
  fit::closure detacher = inspected_commit.CreateDetacher();
  EXPECT_FALSE(on_empty_called);
  detacher();
  EXPECT_TRUE(on_empty_called);

  inspected_commit.set_on_empty(callback::SetWhenCalled(&on_empty_called));
  fit::closure first_detacher = inspected_commit.CreateDetacher();
  EXPECT_FALSE(on_empty_called);
  fit::closure second_detacher = inspected_commit.CreateDetacher();
  EXPECT_FALSE(on_empty_called);
  first_detacher();
  EXPECT_FALSE(on_empty_called);
  second_detacher();
  EXPECT_TRUE(on_empty_called);
}

}  // namespace
}  // namespace ledger
