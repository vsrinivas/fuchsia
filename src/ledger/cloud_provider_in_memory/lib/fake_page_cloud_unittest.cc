// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/cloud_provider_in_memory/lib/fake_page_cloud.h"

#include <fuchsia/ledger/cloud/cpp/fidl.h>

#include <optional>
#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/encoding/encoding.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace ledger {
namespace {

using ::testing::SizeIs;

cloud_provider::Commit MakeCommit(std::string id, std::optional<std::string> diff_parent) {
  cloud_provider::Commit commit;
  commit.set_id(convert::ToArray(id));
  commit.set_data(convert::ToArray("data_" + id));
  if (diff_parent) {
    commit.mutable_diff()->mutable_base_state()->set_at_commit(convert::ToArray(*diff_parent));
  } else {
    commit.mutable_diff()->mutable_base_state()->set_empty_page({});
  }
  commit.mutable_diff()->set_changes({});
  return commit;
}

class FakePageCloudTest : public gtest::TestLoopFixture {
 public:
  FakePageCloudTest() : fake_page_cloud_(dispatcher(), InjectNetworkError::NO) {
    fake_page_cloud_.Bind(page_cloud_.NewRequest());
  }

 protected:
  cloud_provider::PageCloudPtr page_cloud_;

 private:
  FakePageCloud fake_page_cloud_;
};

TEST_F(FakePageCloudTest, DuplicateCommits) {
  // Add one commit.
  cloud_provider::Commits commits;
  commits.commits.push_back(MakeCommit("id0", std::nullopt));
  cloud_provider::CommitPack commit_pack;
  ASSERT_TRUE(ledger::EncodeToBuffer(&commits, &commit_pack.buffer));
  cloud_provider::Status status;
  bool called;
  page_cloud_->AddCommits(std::move(commit_pack),
                          callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  ASSERT_EQ(status, cloud_provider::Status::OK);

  // Add the commit again.
  commits.commits.clear();
  commits.commits.push_back(MakeCommit("id0", std::nullopt));
  ASSERT_TRUE(ledger::EncodeToBuffer(&commits, &commit_pack.buffer));
  page_cloud_->AddCommits(std::move(commit_pack),
                          callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  ASSERT_EQ(status, cloud_provider::Status::OK);

  // Check that the cloud provider only returns one commit.
  std::unique_ptr<cloud_provider::CommitPack> commit_pack_ptr;
  std::unique_ptr<cloud_provider::PositionToken> position_token;
  page_cloud_->GetCommits(nullptr, callback::Capture(callback::SetWhenCalled(&called), &status,
                                                     &commit_pack_ptr, &position_token));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  ASSERT_EQ(status, cloud_provider::Status::OK);
  ASSERT_TRUE(commit_pack_ptr);
  ASSERT_TRUE(ledger::DecodeFromBuffer(commit_pack_ptr->buffer, &commits));
  EXPECT_THAT(commits.commits, SizeIs(1));
}

TEST_F(FakePageCloudTest, RejectUnknownBases) {
  // Add one valid commit, and one with an unknown parent
  cloud_provider::Commits commits;
  commits.commits.push_back(MakeCommit("id0", std::nullopt));
  commits.commits.push_back(MakeCommit("id1", "not a commit"));
  cloud_provider::CommitPack commit_pack;
  ASSERT_TRUE(ledger::EncodeToBuffer(&commits, &commit_pack.buffer));
  cloud_provider::Status status;
  bool called;
  page_cloud_->AddCommits(std::move(commit_pack),
                          callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  ASSERT_EQ(status, cloud_provider::Status::NOT_FOUND);

  // Check that no commit has been added.
  std::unique_ptr<cloud_provider::CommitPack> commit_pack_ptr;
  std::unique_ptr<cloud_provider::PositionToken> position_token;
  page_cloud_->GetCommits(nullptr, callback::Capture(callback::SetWhenCalled(&called), &status,
                                                     &commit_pack_ptr, &position_token));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  ASSERT_EQ(status, cloud_provider::Status::OK);
  ASSERT_TRUE(commit_pack_ptr);
  ASSERT_TRUE(ledger::DecodeFromBuffer(commit_pack_ptr->buffer, &commits));
  EXPECT_THAT(commits.commits, SizeIs(0));
}

TEST_F(FakePageCloudTest, GetDiffForUnknown) {
  bool called;
  cloud_provider::Status status;
  std::unique_ptr<cloud_provider::DiffPack> diff;

  page_cloud_->GetDiff(convert::ToArray("not a commit"), {},
                       callback::Capture(callback::SetWhenCalled(&called), &status, &diff));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, cloud_provider::Status::NOT_FOUND);
}

}  // namespace
}  // namespace ledger
