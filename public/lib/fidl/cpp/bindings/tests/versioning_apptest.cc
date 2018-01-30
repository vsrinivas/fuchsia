// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/public/cpp/application/application_test_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "lib/fidl/compiler/interfaces/tests/versioning_test_client.fidl.h"

namespace fidl {
namespace test {
namespace versioning {

class VersioningApplicationTest : public ApplicationTestBase {
 public:
  VersioningApplicationTest() : ApplicationTestBase() {}
  ~VersioningApplicationTest() override {}

  VersioningApplicationTest(const VersioningApplicationTest&) = delete;
  VersioningApplicationTest& operator=(const VersioningApplicationTest&) = delete;

 protected:
  // ApplicationTestBase overrides.
  void SetUp() override {
    ApplicationTestBase::SetUp();

    ConnectToService(shell(), "mojo:versioning_test_service",
                     database_.NewRequest());
  }

  HumanResourceDatabasePtr database_;
};

TEST_F(VersioningApplicationTest, Struct) {
  // The service side uses a newer version of Employee defintion.
  // The returned struct should be truncated.
  EmployeePtr employee(Employee::New());
  employee->employee_id = 1;
  employee->name = "Homer Simpson";
  employee->department = Department::DEV;

  database_->QueryEmployee(1, true,
                           [&employee](EmployeePtr returned_employee,
                                       Array<uint8_t> returned_finger_print) {
                             EXPECT_TRUE(employee->Equals(*returned_employee));
                             EXPECT_FALSE(returned_finger_print.is_null());
                           });
  database_.WaitForResponse();

  // Passing a struct of older version to the service side works.
  EmployeePtr new_employee(Employee::New());
  new_employee->employee_id = 2;
  new_employee->name = "Marge Simpson";
  new_employee->department = Department::SALES;

  database_->AddEmployee(new_employee.Clone(),
                         [](bool success) { EXPECT_TRUE(success); });
  database_.WaitForResponse();

  database_->QueryEmployee(
      2, false, [&new_employee](EmployeePtr returned_employee,
                                Array<uint8_t> returned_finger_print) {
        EXPECT_TRUE(new_employee->Equals(*returned_employee));
        EXPECT_TRUE(returned_finger_print.is_null());
      });
  database_.WaitForResponse();
}

TEST_F(VersioningApplicationTest, QueryVersion) {
  EXPECT_EQ(0u, database_.version());
  database_.QueryVersion([](uint32_t version) { EXPECT_EQ(1u, version); });
  database_.WaitForResponse();
  EXPECT_EQ(1u, database_.version());
}

TEST_F(VersioningApplicationTest, RequireVersion) {
  EXPECT_EQ(0u, database_.version());

  database_.RequireVersion(1);
  EXPECT_EQ(1u, database_.version());
  database_->QueryEmployee(3, false,
                           [](EmployeePtr returned_employee,
                              Array<uint8_t> returned_finger_print) {});
  database_.WaitForResponse();
  EXPECT_FALSE(database_.encountered_error());

  // Requiring a version higher than what the service side implements will close
  // the pipe.
  database_.RequireVersion(3);
  EXPECT_EQ(3u, database_.version());
  database_->QueryEmployee(1, false,
                           [](EmployeePtr returned_employee,
                              Array<uint8_t> returned_finger_print) {});
  database_.WaitForResponse();
  EXPECT_TRUE(database_.encountered_error());
}

TEST_F(VersioningApplicationTest, CallNonexistentMethod) {
  EXPECT_EQ(0u, database_.version());

  auto new_finger_print = Array<uint8_t>::New(128);
  for (size_t i = 0; i < 128; ++i)
    new_finger_print[i] = i + 13;

  // Although the client side doesn't know whether the service side supports
  // version 1, calling a version 1 method succeeds as long as the service side
  // supports version 1.
  database_->AttachFingerPrint(1, new_finger_print.Clone(),
                               [](bool success) { EXPECT_TRUE(success); });
  database_.WaitForResponse();

  // Calling a version 2 method (which the service side doesn't support) closes
  // the pipe.
  database_->ListEmployeeIds([](Array<uint64_t> ids) { EXPECT_TRUE(false); });
  database_.WaitForResponse();
  EXPECT_TRUE(database_.encountered_error());
}

}  // namespace versioning
}  // namespace examples
}  // namespace fidl
