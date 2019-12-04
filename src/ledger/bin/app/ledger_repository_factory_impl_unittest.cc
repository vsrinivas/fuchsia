// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/ledger_repository_factory_impl.h"

#include <fuchsia/inspect/deprecated/cpp/fidl.h>
#include <lib/async/cpp/task.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/inspect/inspect.h"
#include "src/ledger/bin/platform/detached_path.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fsl/io/fd.h"
#include "src/lib/inspect_deprecated/inspect.h"
#include "src/lib/inspect_deprecated/reader.h"
#include "src/lib/inspect_deprecated/testing/inspect.h"

namespace ledger {
namespace {

using ::inspect_deprecated::testing::ChildrenMatch;
using ::inspect_deprecated::testing::MetricList;
using ::inspect_deprecated::testing::NameMatches;
using ::inspect_deprecated::testing::NodeMatches;
using ::inspect_deprecated::testing::PropertyList;
using ::inspect_deprecated::testing::UIntMetricIs;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::UnorderedElementsAre;

constexpr char kTestTopLevelNodeName[] = "top-level-of-test node";
constexpr char kUserID[] = "test user ID";

class LedgerRepositoryFactoryImplTest : public TestWithEnvironment {
 public:
  LedgerRepositoryFactoryImplTest() {
    top_level_inspect_node_ = inspect_deprecated::Node(kTestTopLevelNodeName);
    repository_factory_ = std::make_unique<LedgerRepositoryFactoryImpl>(
        &environment_, nullptr,
        top_level_inspect_node_.CreateChild(convert::ToString(kRepositoriesInspectPathComponent)));
  }

  LedgerRepositoryFactoryImplTest(const LedgerRepositoryFactoryImplTest&) = delete;
  LedgerRepositoryFactoryImplTest& operator=(const LedgerRepositoryFactoryImplTest&) = delete;
  ~LedgerRepositoryFactoryImplTest() override = default;

  void TearDown() override {
    // Closing the factory, as the destruction must happen on the loop.
    CloseFactory(std::move(repository_factory_));
  }

 protected:
  ::testing::AssertionResult CreateDirectory(const std::string& name);
  ::testing::AssertionResult CallGetRepository(
      const std::string& name, ledger_internal::LedgerRepositoryPtr* ledger_repository_ptr);

  // Helper function to call the |Close| method on a
  // |LedgerRepositoryFactoryImpl|. This is needed as the |Close| method must be
  // called while the loop is running.
  void CloseFactory(std::unique_ptr<LedgerRepositoryFactoryImpl> factory);

  scoped_tmpfs::ScopedTmpFS tmpfs_;
  inspect_deprecated::Node top_level_inspect_node_;
  std::unique_ptr<LedgerRepositoryFactoryImpl> repository_factory_;
};

::testing::AssertionResult LedgerRepositoryFactoryImplTest::CreateDirectory(
    const std::string& name) {
  if (!environment_.file_system()->CreateDirectory(ledger::DetachedPath(tmpfs_.root_fd(), name))) {
    return ::testing::AssertionFailure() << "Failed to create directory \"" << name << "\"!";
  }
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult LedgerRepositoryFactoryImplTest::CallGetRepository(
    const std::string& name, ledger_internal::LedgerRepositoryPtr* ledger_repository_ptr) {
  fbl::unique_fd fd(openat(tmpfs_.root_fd(), name.c_str(), O_RDONLY));
  if (!fd.is_valid()) {
    return ::testing::AssertionFailure() << "Failed to validate directory \"" << name << "\"!";
  }

  bool callback_called;
  Status status;

  repository_factory_->GetRepository(
      fsl::CloneChannelFromFileDescriptor(fd.get()), nullptr, kUserID,
      ledger_repository_ptr->NewRequest(),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status));

  RunLoopUntilIdle();

  if (!callback_called) {
    return ::testing::AssertionFailure() << "Callback passed to GetRepository not called!";
  }
  if (status != Status::OK) {
    return ::testing::AssertionFailure()
           << "Status of GetRepository call was " << static_cast<int32_t>(status) << "!";
  }
  return ::testing::AssertionSuccess();
}

void LedgerRepositoryFactoryImplTest::CloseFactory(
    std::unique_ptr<LedgerRepositoryFactoryImpl> factory) {
  async::PostTask(dispatcher(), [factory = std::move(factory)]() mutable { factory.reset(); });
  RunLoopUntilIdle();
}

TEST_F(LedgerRepositoryFactoryImplTest, InspectAPINoRepositories) {
  auto hierarchy = inspect_deprecated::ReadFromObject(top_level_inspect_node_);
  EXPECT_THAT(hierarchy, AllOf(NodeMatches(AllOf(NameMatches(kTestTopLevelNodeName),
                                                 MetricList(IsEmpty()), PropertyList(IsEmpty()))),
                               ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(NameMatches(
                                   convert::ToString(kRepositoriesInspectPathComponent))))))));
}

TEST_F(LedgerRepositoryFactoryImplTest, InspectAPITwoRepositoriesOneAccessedTwice) {
  // The directories in which the two repositories will be created.
  std::string first_directory = "first directory";
  std::string second_directory = "second directory";

  // The names of the two repositories, determined by the
  // LedgerRepositoryFactoryImpl under test.
  std::string first_repository_name;
  std::string second_repository_name;

  // Bindings to the two repositories. If these are not maintained, the
  // LedgerRepositoryFactoryImpl::LedgerRepositoryContainer objects associated
  // with the repositories will be destroyed and the repositories will no
  // longer appear represented in the Inspect API.
  ledger_internal::LedgerRepositoryPtr first_ledger_repository_ptr;
  ledger_internal::LedgerRepositoryPtr second_ledger_repository_ptr;
  ledger_internal::LedgerRepositoryPtr first_again_ledger_repository_ptr;

  // Create the directories for the repositories.
  ASSERT_TRUE(CreateDirectory(first_directory));
  ASSERT_TRUE(CreateDirectory(second_directory));

  // Request one repository, then query the top_level_inspect_node_ (and its
  // children) to verify that that repository is listed (and to learn the name
  // under which it is listed) and that it was requested once.
  ASSERT_TRUE(CallGetRepository(first_directory, &first_ledger_repository_ptr));
  auto top_hierarchy = inspect_deprecated::ReadFromObject(top_level_inspect_node_);
  auto lone_repository_match = NodeMatches(
      MetricList(Contains(UIntMetricIs(convert::ToString(kRequestsInspectPathComponent), 1UL))));
  auto first_inspection_repositories_match =
      AllOf(NodeMatches(NameMatches(convert::ToString(kRepositoriesInspectPathComponent))),
            ChildrenMatch(UnorderedElementsAre(lone_repository_match)));
  auto first_inspection_top_level_match =
      ChildrenMatch(UnorderedElementsAre(first_inspection_repositories_match));
  EXPECT_THAT(top_hierarchy, first_inspection_top_level_match);
  first_repository_name = top_hierarchy.children()[0].children()[0].node().name();
  EXPECT_THAT(first_repository_name, Not(IsEmpty()));

  // Request a second repository, then query the "repositories" Inspect object
  // to verify that that second repository is listed in addition to the first
  // (and to learn the name under which it is listed) and that the two
  // repositories were each requested once.
  ASSERT_TRUE(CallGetRepository(second_directory, &second_ledger_repository_ptr));
  top_hierarchy = inspect_deprecated::ReadFromObject(top_level_inspect_node_);
  auto second_inspection_two_repositories_match = UnorderedElementsAre(
      NodeMatches(AllOf(NameMatches(first_repository_name),
                        MetricList(Contains(
                            UIntMetricIs(convert::ToString(kRequestsInspectPathComponent), 1UL))))),
      NodeMatches(MetricList(
          Contains(UIntMetricIs(convert::ToString(kRequestsInspectPathComponent), 1UL)))));
  auto second_inspection_repositories_match = UnorderedElementsAre(
      AllOf(NodeMatches(NameMatches(convert::ToString(kRepositoriesInspectPathComponent))),
            ChildrenMatch(second_inspection_two_repositories_match)));
  auto second_inspection_top_level_match = ChildrenMatch(second_inspection_repositories_match);
  EXPECT_THAT(top_hierarchy, second_inspection_top_level_match);
  second_repository_name =
      find_if_not(top_hierarchy.children()[0].children().begin(),
                  top_hierarchy.children()[0].children().end(),
                  [&first_repository_name](
                      const inspect_deprecated::ObjectHierarchy& repository_hierarchy) {
                    return repository_hierarchy.node().name() == first_repository_name;
                  })
          ->node()
          .name();
  EXPECT_THAT(second_repository_name, Not(IsEmpty()));

  // Request the first repository a second time, then query the "repositories"
  // Inspect object to verify that both repositories remain listed (with their
  // same names) and are described as having been requested twice and once,
  // respectively.
  ASSERT_TRUE(CallGetRepository(first_directory, &first_again_ledger_repository_ptr));
  top_hierarchy = inspect_deprecated::ReadFromObject(top_level_inspect_node_);
  auto third_inspection_two_repositories_match = UnorderedElementsAre(
      NodeMatches(AllOf(NameMatches(first_repository_name),
                        MetricList(Contains(
                            UIntMetricIs(convert::ToString(kRequestsInspectPathComponent), 2UL))))),
      NodeMatches(AllOf(NameMatches(second_repository_name),
                        MetricList(Contains(UIntMetricIs(
                            convert::ToString(kRequestsInspectPathComponent), 1UL))))));
  auto third_inspection_repositories_match = UnorderedElementsAre(
      AllOf(NodeMatches(NameMatches(convert::ToString(kRepositoriesInspectPathComponent))),
            ChildrenMatch(third_inspection_two_repositories_match)));
  auto third_inspection_top_level_match = ChildrenMatch(third_inspection_repositories_match);
  EXPECT_THAT(top_hierarchy, third_inspection_top_level_match);
}

// Verifies that closing a ledger repository closes the LedgerRepository
// connections once all Ledger connections are themselves closed. This test is
// reproduced here due to the interaction between |LedgerRepositoryImpl| and
// |LedgerRepositoryFactoryImpl|.
TEST_F(LedgerRepositoryFactoryImplTest, CloseLedgerRepository) {
  std::string repository_directory = "directory";
  ASSERT_TRUE(CreateDirectory(repository_directory));

  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr1;
  ASSERT_TRUE(CallGetRepository(repository_directory, &ledger_repository_ptr1));

  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr2;
  ASSERT_TRUE(CallGetRepository(repository_directory, &ledger_repository_ptr2));

  ledger::LedgerPtr ledger_ptr;

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
  EXPECT_FALSE(ptr1_closed);
  EXPECT_FALSE(ptr2_closed);
  EXPECT_FALSE(ledger_closed);

  ledger_repository_ptr2->Close();
  RunLoopUntilIdle();
  EXPECT_FALSE(ptr1_closed);
  EXPECT_FALSE(ptr2_closed);
  EXPECT_FALSE(ledger_closed);

  ledger_ptr.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(ptr1_closed);
  EXPECT_TRUE(ptr2_closed);

  EXPECT_EQ(ptr1_closed_status, ZX_OK);
  EXPECT_EQ(ptr2_closed_status, ZX_OK);
}

// Verifies that closing LedgerRepositoryFactoryImpl closes all the elements under it.
TEST_F(LedgerRepositoryFactoryImplTest, CloseFactory) {
  auto top_level_inspect_node = inspect_deprecated::Node(kTestTopLevelNodeName);
  auto repository_factory = std::make_unique<LedgerRepositoryFactoryImpl>(
      &environment_, nullptr,
      top_level_inspect_node.CreateChild(convert::ToString(kRepositoriesInspectPathComponent)));

  std::unique_ptr<scoped_tmpfs::ScopedTmpFS> tmpfs = std::make_unique<scoped_tmpfs::ScopedTmpFS>();
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr;

  bool get_repository_called;
  Status status;

  repository_factory->GetRepository(
      fsl::CloneChannelFromFileDescriptor(tmpfs->root_fd()), nullptr, "",
      ledger_repository_ptr.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_repository_called), &status));

  bool channel_closed;
  zx_status_t zx_status;

  ledger_repository_ptr.set_error_handler(
      callback::Capture(callback::SetWhenCalled(&channel_closed), &zx_status));

  RunLoopUntilIdle();

  EXPECT_TRUE(get_repository_called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_FALSE(channel_closed);

  CloseFactory(std::move(repository_factory));

  EXPECT_TRUE(channel_closed);
}

}  // namespace
}  // namespace ledger
