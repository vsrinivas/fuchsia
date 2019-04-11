// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/ledger_repository_factory_impl.h"

#include <fuchsia/inspect/cpp/fidl.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/fsl/io/fd.h>
#include <lib/inspect/inspect.h>
#include <lib/inspect/reader.h>
#include <lib/inspect/testing/inspect.h>
#include <src/lib/fxl/strings/string_view.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/unique_fd.h"

namespace ledger {
namespace {

using ::inspect::testing::ChildrenMatch;
using ::inspect::testing::MetricList;
using ::inspect::testing::NameMatches;
using ::inspect::testing::NodeMatches;
using ::inspect::testing::PropertyList;
using ::inspect::testing::UIntMetricIs;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::UnorderedElementsAre;

constexpr char kObjectsName[] = "test objects";
constexpr char kUserID[] = "test user ID";

class LedgerRepositoryFactoryImplTest : public TestWithEnvironment {
 public:
  LedgerRepositoryFactoryImplTest() {
    top_level_inspect_object_ = inspect::Object(kObjectsName);
    repository_factory_ = std::make_unique<LedgerRepositoryFactoryImpl>(
        &environment_, nullptr,
        top_level_inspect_object_.CreateChild(
            kRepositoriesInspectPathComponent));
  }

  ~LedgerRepositoryFactoryImplTest() override {}

 protected:
  ::testing::AssertionResult CreateDirectory(const std::string& name);
  ::testing::AssertionResult CallGetRepository(
      const std::string& name,
      ledger_internal::LedgerRepositoryPtr* ledger_repository_ptr);

  scoped_tmpfs::ScopedTmpFS tmpfs_;
  inspect::Object top_level_inspect_object_;
  std::unique_ptr<LedgerRepositoryFactoryImpl> repository_factory_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryFactoryImplTest);
};

::testing::AssertionResult LedgerRepositoryFactoryImplTest::CreateDirectory(
    const std::string& name) {
  if (!files::CreateDirectoryAt(tmpfs_.root_fd(), name)) {
    return ::testing::AssertionFailure()
           << "Failed to create directory \"" << name << "\"!";
  }
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult LedgerRepositoryFactoryImplTest::CallGetRepository(
    const std::string& name,
    ledger_internal::LedgerRepositoryPtr* ledger_repository_ptr) {
  fxl::UniqueFD fd(openat(tmpfs_.root_fd(), name.c_str(), O_RDONLY));
  if (!fd.is_valid()) {
    return ::testing::AssertionFailure()
           << "Failed to validate directory \"" << name << "\"!";
  }

  bool callback_called;
  Status status = Status::UNKNOWN_ERROR;

  repository_factory_->GetRepository(
      fsl::CloneChannelFromFileDescriptor(fd.get()), nullptr, kUserID,
      ledger_repository_ptr->NewRequest(),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status));

  if (!callback_called) {
    return ::testing::AssertionFailure()
           << "Callback passed to GetRepository not called!";
  }
  if (status != Status::OK) {
    return ::testing::AssertionFailure() << "Status of GetRepository call was "
                                         << static_cast<int32_t>(status) << "!";
  }
  return ::testing::AssertionSuccess();
}

TEST_F(LedgerRepositoryFactoryImplTest, InspectAPINoRepositories) {
  auto hierarchy = inspect::ReadFromObject(top_level_inspect_object_);
  EXPECT_THAT(
      hierarchy,
      AllOf(NodeMatches(AllOf(NameMatches(kObjectsName), MetricList(IsEmpty()),
                              PropertyList(IsEmpty()))),
            ChildrenMatch(UnorderedElementsAre(NodeMatches(
                AllOf(NameMatches(kRepositoriesInspectPathComponent)))))));
}

TEST_F(LedgerRepositoryFactoryImplTest,
       InspectAPITwoRepositoriesOneAccessedTwice) {
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

  // Request one repository, then query the top_level_inspect_object_ (and its
  // children) to verify that that repository is listed (and to learn the name
  // under which it is listed) and that it was requested once.
  ASSERT_TRUE(CallGetRepository(first_directory, &first_ledger_repository_ptr));
  auto top_hierarchy = inspect::ReadFromObject(top_level_inspect_object_);
  auto lone_repository_match = NodeMatches(
      MetricList(Contains(UIntMetricIs(kRequestsInspectPathComponent, 1UL))));
  auto first_inspection_repositories_match =
      AllOf(NodeMatches(NameMatches(kRepositoriesInspectPathComponent)),
            ChildrenMatch(UnorderedElementsAre(lone_repository_match)));
  auto first_inspection_top_level_match =
      ChildrenMatch(UnorderedElementsAre(first_inspection_repositories_match));
  EXPECT_THAT(top_hierarchy, first_inspection_top_level_match);
  first_repository_name =
      top_hierarchy.children()[0].children()[0].node().name();
  EXPECT_THAT(first_repository_name, Not(IsEmpty()));

  // Request a second repository, then query the "repositories" Inspect object
  // to verify that that second repository is listed in addition to the first
  // (and to learn the name under which it is listed) and that the two
  // repositories were each requested once.
  ASSERT_TRUE(
      CallGetRepository(second_directory, &second_ledger_repository_ptr));
  top_hierarchy = inspect::ReadFromObject(top_level_inspect_object_);
  auto second_inspection_two_repositories_match = UnorderedElementsAre(
      NodeMatches(AllOf(NameMatches(first_repository_name),
                        MetricList(Contains(UIntMetricIs(
                            kRequestsInspectPathComponent, 1UL))))),
      NodeMatches(MetricList(
          Contains(UIntMetricIs(kRequestsInspectPathComponent, 1UL)))));
  auto second_inspection_repositories_match = UnorderedElementsAre(
      AllOf(NodeMatches(NameMatches(kRepositoriesInspectPathComponent)),
            ChildrenMatch(second_inspection_two_repositories_match)));
  auto second_inspection_top_level_match =
      ChildrenMatch(second_inspection_repositories_match);
  EXPECT_THAT(top_hierarchy, second_inspection_top_level_match);
  second_repository_name =
      find_if_not(top_hierarchy.children()[0].children().begin(),
                  top_hierarchy.children()[0].children().end(),
                  [&first_repository_name](
                      const inspect::ObjectHierarchy& repository_hierarchy) {
                    return repository_hierarchy.node().name() ==
                           first_repository_name;
                  })
          ->node()
          .name();
  EXPECT_THAT(second_repository_name, Not(IsEmpty()));

  // Request the first repository a second time, then query the "repositories"
  // Inspect object to verify that both repositories remain listed (with their
  // same names) and are described as having been requested twice and once,
  // respectively.
  ASSERT_TRUE(
      CallGetRepository(first_directory, &first_again_ledger_repository_ptr));
  top_hierarchy = inspect::ReadFromObject(top_level_inspect_object_);
  auto third_inspection_two_repositories_match = UnorderedElementsAre(
      NodeMatches(AllOf(NameMatches(first_repository_name),
                        MetricList(Contains(UIntMetricIs(
                            kRequestsInspectPathComponent, 2UL))))),
      NodeMatches(AllOf(NameMatches(second_repository_name),
                        MetricList(Contains(UIntMetricIs(
                            kRequestsInspectPathComponent, 1UL))))));
  auto third_inspection_repositories_match = UnorderedElementsAre(
      AllOf(NodeMatches(NameMatches(kRepositoriesInspectPathComponent)),
            ChildrenMatch(third_inspection_two_repositories_match)));
  auto third_inspection_top_level_match =
      ChildrenMatch(third_inspection_repositories_match);
  EXPECT_THAT(top_hierarchy, third_inspection_top_level_match);
}

TEST_F(LedgerRepositoryFactoryImplTest, CloseOnFilesystemUnavailable) {
  std::unique_ptr<scoped_tmpfs::ScopedTmpFS> tmpfs =
      std::make_unique<scoped_tmpfs::ScopedTmpFS>();
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr;

  bool get_repository_called;
  Status status = Status::UNKNOWN_ERROR;

  repository_factory_->GetRepository(
      fsl::CloneChannelFromFileDescriptor(tmpfs->root_fd()), nullptr, "",
      ledger_repository_ptr.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_repository_called),
                        &status));

  bool channel_closed;
  zx_status_t zx_status;

  ledger_repository_ptr.set_error_handler(
      callback::Capture(callback::SetWhenCalled(&channel_closed), &zx_status));

  RunLoopUntilIdle();

  EXPECT_TRUE(get_repository_called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(channel_closed);

  tmpfs.reset();

  RunLoopUntilIdle();

  EXPECT_TRUE(channel_closed);
}

}  // namespace
}  // namespace ledger
