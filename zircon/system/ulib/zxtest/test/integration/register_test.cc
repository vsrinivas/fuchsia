// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper.h"

#include <zircon/assert.h>
#include <zxtest/base/test-info.h>
#include <zxtest/zxtest.h>

namespace {
TEST(Test, AutoRegister) {
}

class TestFixture : public zxtest::Test {
 public:
  ~TestFixture() override {
  }

  static void SetUpTestCase() {
  }
  static void TearDownTestCase() {
  }

 protected:
  void SetUp() override {
  }
  void TearDown() override {
  }
};

TEST_F(TestFixture, AutoRegister) {
}

void Verify() {
  auto* runner = zxtest::Runner::GetInstance();

  // This is using internals to obtain a handle on the test info, through the TestRef.
  // This will either fail at compile time because this variable does not exist(macro error),
  // or at runtime because the test failed to register (logic error).
  const zxtest::TestInfo& test_info = runner->GetTestInfo(_ZXTEST_TEST_REF(Test, AutoRegister));
  ZX_ASSERT_MSG(test_info.name() == "AutoRegister", "TEST registered test with the wrong name.");
  const zxtest::TestInfo& fixture_info =
      runner->GetTestInfo(_ZXTEST_TEST_REF(TestFixture, AutoRegister));
  ZX_ASSERT_MSG(fixture_info.name() == "AutoRegister",
                "TEST_F registered test with the wrong name.");
}

void Register() __attribute__((constructor));
void Register() {
  zxtest::test::AddCheckFunction(&Verify);
}

}  // namespace
