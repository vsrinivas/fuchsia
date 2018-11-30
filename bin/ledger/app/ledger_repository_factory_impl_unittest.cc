// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/ledger_repository_factory_impl.h"

#include <fuchsia/inspect/cpp/fidl.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/fsl/io/fd.h>
#include <lib/fxl/files/directory.h>
#include <lib/fxl/files/unique_fd.h>
#include <lib/fxl/strings/string_view.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/bin/ledger/testing/inspect.h"
#include "peridot/bin/ledger/testing/test_with_environment.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"

namespace ledger {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;

constexpr fxl::StringView kObjectsName = "test objects";
constexpr fxl::StringView kRepositoriesName = "repositories";
constexpr fxl::StringView kUserID = "test user ID";

class LedgerRepositoryFactoryImplTest : public TestWithEnvironment {
 public:
  LedgerRepositoryFactoryImplTest() {
    object_dir_ = component::ObjectDir(
        fbl::MakeRefCounted<component::Object>(kObjectsName));
    repository_factory_ = std::make_unique<LedgerRepositoryFactoryImpl>(
        &environment_, nullptr, object_dir_);
  }

  ~LedgerRepositoryFactoryImplTest() override {}

 protected:
  ::testing::AssertionResult CreateDirectory(std::string name);
  ::testing::AssertionResult CallGetRepository(
      std::string name,
      ledger_internal::LedgerRepositoryPtr* ledger_repository_ptr);
  ::testing::AssertionResult ReadTopLevelData(fuchsia::inspect::Object* object);
  ::testing::AssertionResult ListTopLevelChildren(
      fidl::VectorPtr<fidl::StringPtr>* children);
  ::testing::AssertionResult OpenTopLevelRepositoriesChild(
      fuchsia::inspect::InspectPtr* repositories_inspect_ptr);
  ::testing::AssertionResult ReadData(fuchsia::inspect::InspectPtr* inspect_ptr,
                                      fuchsia::inspect::Object* object);
  ::testing::AssertionResult ListChildren(
      fuchsia::inspect::InspectPtr* inspect_ptr,
      fidl::VectorPtr<fidl::StringPtr>* children_names);
  ::testing::AssertionResult OpenChild(
      fuchsia::inspect::InspectPtr* parent_inspect_ptr,
      fidl::StringPtr child_name,
      fuchsia::inspect::InspectPtr* child_inspect_ptr);

  scoped_tmpfs::ScopedTmpFS tmpfs_;
  component::ObjectDir object_dir_;
  std::unique_ptr<LedgerRepositoryFactoryImpl> repository_factory_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryFactoryImplTest);
};

::testing::AssertionResult LedgerRepositoryFactoryImplTest::CreateDirectory(
    std::string name) {
  if (!files::CreateDirectoryAt(tmpfs_.root_fd(), name)) {
    return ::testing::AssertionFailure()
           << "Failed to create directory \"" << name << "\"!";
  }
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult LedgerRepositoryFactoryImplTest::CallGetRepository(
    std::string name,
    ledger_internal::LedgerRepositoryPtr* ledger_repository_ptr) {
  fxl::UniqueFD fd(openat(tmpfs_.root_fd(), name.c_str(), O_PATH));
  if (!fd.is_valid()) {
    return ::testing::AssertionFailure()
           << "Failed to validate directory \"" << name << "\"!";
  }

  bool callback_called;
  Status status = Status::UNKNOWN_ERROR;

  repository_factory_->GetRepository(
      fsl::CloneChannelFromFileDescriptor(fd.get()), nullptr,
      kUserID.ToString(), ledger_repository_ptr->NewRequest(),
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

::testing::AssertionResult LedgerRepositoryFactoryImplTest::ReadTopLevelData(
    fuchsia::inspect::Object* object) {
  bool callback_called;

  object_dir_.object()->ReadData(
      callback::Capture(callback::SetWhenCalled(&callback_called), object));
  RunLoopUntilIdle();

  if (!callback_called) {
    return ::testing::AssertionFailure()
           << "Callback passed to object_dir_.object()->ReadData not called!";
  }
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult
LedgerRepositoryFactoryImplTest::ListTopLevelChildren(
    fidl::VectorPtr<fidl::StringPtr>* children) {
  bool callback_called;

  object_dir_.object()->ListChildren(
      callback::Capture(callback::SetWhenCalled(&callback_called), children));
  RunLoopUntilIdle();

  if (!callback_called) {
    return ::testing::AssertionFailure()
           << "Callback passed to object_dir_.object()->ListChildren not "
              "called!";
  }
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult
LedgerRepositoryFactoryImplTest::OpenTopLevelRepositoriesChild(
    fuchsia::inspect::InspectPtr* repositories_inspect_ptr) {
  bool callback_called;
  bool success = false;

  object_dir_.object()->OpenChild(
      kRepositoriesName.ToString(), repositories_inspect_ptr->NewRequest(),
      callback::Capture(callback::SetWhenCalled(&callback_called), &success));
  RunLoopUntilIdle();

  if (!callback_called) {
    return ::testing::AssertionFailure()
           << "Callback passed to object_dir_.object()->OpenChild not called!";
  }
  if (!success) {
    return ::testing::AssertionFailure()
           << "object_dir_.object()->OpenChild call unsuccessful!";
  }
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult LedgerRepositoryFactoryImplTest::ReadData(
    fuchsia::inspect::InspectPtr* inspect_ptr,
    fuchsia::inspect::Object* object) {
  bool callback_called;

  (*inspect_ptr)
      ->ReadData(
          callback::Capture(callback::SetWhenCalled(&callback_called), object));
  RunLoopUntilIdle();

  if (!callback_called) {
    return ::testing::AssertionFailure() << "ReadData callback not called!";
  }
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult LedgerRepositoryFactoryImplTest::ListChildren(
    fuchsia::inspect::InspectPtr* inspect_ptr,
    fidl::VectorPtr<fidl::StringPtr>* children_names) {
  bool callback_called;

  (*inspect_ptr)
      ->ListChildren(callback::Capture(
          callback::SetWhenCalled(&callback_called), children_names));
  RunLoopUntilIdle();

  if (!callback_called) {
    return ::testing::AssertionFailure() << "ListChildren callback not called!";
  }
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult LedgerRepositoryFactoryImplTest::OpenChild(
    fuchsia::inspect::InspectPtr* parent_inspect_ptr,
    fidl::StringPtr child_name,
    fuchsia::inspect::InspectPtr* child_inspect_ptr) {
  bool callback_called;
  bool success = false;

  (*parent_inspect_ptr)
      ->OpenChild(child_name, child_inspect_ptr->NewRequest(),
                  callback::Capture(callback::SetWhenCalled(&callback_called),
                                    &success));
  RunLoopUntilIdle();

  if (!callback_called) {
    return ::testing::AssertionFailure() << "OpenChild callback not called!";
  }
  if (!success) {
    return ::testing::AssertionFailure() << "OpenChild call unsuccessful!";
  }
  return ::testing::AssertionSuccess();
}

TEST_F(LedgerRepositoryFactoryImplTest, InspectAPINoRepositories) {
  fuchsia::inspect::Object object;
  fidl::VectorPtr<fidl::StringPtr> children;

  ASSERT_TRUE(ReadTopLevelData(&object));
  ASSERT_TRUE(ListTopLevelChildren(&children));

  EXPECT_EQ(kObjectsName, *object.name);
  EXPECT_THAT(*object.properties, IsEmpty());
  EXPECT_THAT(*object.metrics, IsEmpty());
  EXPECT_THAT(*children, IsEmpty());
}

TEST_F(LedgerRepositoryFactoryImplTest,
       InspectAPITwoRepositoriesOneAccessedTwice) {
  // The directories in which the two repositories will be created.
  std::string first_directory = "first directory";
  std::string second_directory = "second directory";

  // The names of the two repositories, determined by the
  // LedgerRepositoryFactoryImpl under test.
  fidl::StringPtr first_repository_name;
  fidl::StringPtr second_repository_name;

  // Bindings to the two repositories. If these are not maintained, the
  // LedgerRepositoryFactoryImpl::LedgerRepositoryContainer objects associated
  // with the repositories will be destroyed and the repositories will no
  // longer appear represented in the Inspect API.
  ledger_internal::LedgerRepositoryPtr first_ledger_repository_ptr;
  ledger_internal::LedgerRepositoryPtr second_ledger_repository_ptr;
  ledger_internal::LedgerRepositoryPtr first_again_ledger_repository_ptr;

  // Bindings to Inspect API "Inspect" objects. Over the course of the test the
  // top-level object_dir_ will gain a "repositories" child which itself will
  // gain two children (one for each created repository, with names chosen by
  // the LedgerRepositoryFactoryImpl under test).
  fuchsia::inspect::InspectPtr repositories_inspect_ptr;
  fuchsia::inspect::InspectPtr first_repository_inspect_ptr;
  fuchsia::inspect::InspectPtr second_repository_inspect_ptr;

  // Temporary objects populated and cleared throughout the test.
  fuchsia::inspect::Object object;
  fidl::VectorPtr<fidl::StringPtr> children_names;

  // Create the directories for the repositories.
  ASSERT_TRUE(CreateDirectory(first_directory));
  ASSERT_TRUE(CreateDirectory(second_directory));

  // Request one repository, then query the object_dir_ (and its children) to
  // verify that that repository is listed (and to learn the name under which
  // it is listed) and that it was requested once.
  ASSERT_TRUE(CallGetRepository(first_directory, &first_ledger_repository_ptr));
  ASSERT_TRUE(ListTopLevelChildren(&children_names));
  EXPECT_THAT(*children_names, ElementsAre(kRepositoriesName.ToString()));
  ASSERT_TRUE(OpenTopLevelRepositoriesChild(&repositories_inspect_ptr));
  ASSERT_TRUE(ListChildren(&repositories_inspect_ptr, &children_names));
  EXPECT_THAT(*children_names, SizeIs(1));
  first_repository_name = children_names->at(0);
  EXPECT_THAT(*first_repository_name, Not(IsEmpty()));
  ASSERT_TRUE(OpenChild(&repositories_inspect_ptr, first_repository_name,
                        &first_repository_inspect_ptr));
  ASSERT_TRUE(ReadData(&first_repository_inspect_ptr, &object));
  EXPECT_EQ(first_repository_name, *object.name);
  ExpectRequestsMetric(&object, 1UL);

  // Request a second repository, then query the "repositories" Inspect object
  // to verify that that second repository is listed in addition to the first
  // (and to learn the name under which it is listed) and that the two
  // repositories were each requested once.
  ASSERT_TRUE(
      CallGetRepository(second_directory, &second_ledger_repository_ptr));
  ASSERT_TRUE(ListChildren(&repositories_inspect_ptr, &children_names));
  EXPECT_THAT(*children_names, SizeIs(2));
  second_repository_name =
      *find_if_not(children_names->begin(), children_names->end(),
                   [&first_repository_name](const auto& name) {
                     return name == first_repository_name;
                   });
  EXPECT_THAT(*children_names, UnorderedElementsAre(first_repository_name,
                                                    second_repository_name));
  EXPECT_THAT(*second_repository_name, Not(IsEmpty()));
  ASSERT_TRUE(OpenChild(&repositories_inspect_ptr, second_repository_name,
                        &second_repository_inspect_ptr));
  ASSERT_TRUE(ReadData(&first_repository_inspect_ptr, &object));
  EXPECT_EQ(first_repository_name, *object.name);
  ExpectRequestsMetric(&object, 1UL);
  ASSERT_TRUE(ReadData(&second_repository_inspect_ptr, &object));
  EXPECT_EQ(second_repository_name, *object.name);
  ExpectRequestsMetric(&object, 1UL);

  // Request the first repository a second time, then query the "repositories"
  // Inspect object to verify that both repositories remain listed (with their
  // same names) and are described as having been requested twice and once,
  // respectively.
  ASSERT_TRUE(
      CallGetRepository(first_directory, &first_again_ledger_repository_ptr));
  ASSERT_TRUE(ListChildren(&repositories_inspect_ptr, &children_names));
  EXPECT_THAT(*children_names, UnorderedElementsAre(first_repository_name,
                                                    second_repository_name));
  ASSERT_TRUE(ReadData(&first_repository_inspect_ptr, &object));
  EXPECT_EQ(first_repository_name, *object.name);
  ExpectRequestsMetric(&object, 2UL);
  ASSERT_TRUE(ReadData(&second_repository_inspect_ptr, &object));
  EXPECT_EQ(second_repository_name, *object.name);
  ExpectRequestsMetric(&object, 1UL);
}

}  // namespace
}  // namespace ledger
