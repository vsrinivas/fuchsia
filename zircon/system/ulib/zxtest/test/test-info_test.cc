// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test-registry.h"

#include <cstdlib>
#include <memory>

#include <zircon/assert.h>
#include <zxtest/base/test-driver.h>
#include <zxtest/base/test-info.h>
#include <zxtest/base/test.h>
#include <zxtest/base/types.h>

namespace zxtest {
using internal::TestDriver;
namespace test {
namespace {

constexpr SourceLocation kSourceLocation = {.filename = "myfilepath.cpp",
                                            .line_number = 4815162342};
constexpr char kTestName[] = "TestInfoTest";

class FakeTest : public zxtest::Test {
 public:
  bool* called_;

 private:
  void TestBody() final {
    *called_ = true;
  }
};

}  // namespace

void TestInfoDefault() {
  TestInfo info(kTestName, kSourceLocation, &zxtest::Test::Create<FakeTest>);

  ZX_ASSERT_MSG(info.name() == kTestName, "TestInfo name is not set correctly.");
  ZX_ASSERT_MSG(std::memcmp(&info.location(), &kSourceLocation, sizeof(SourceLocation)) == 0,
                "TestInfo source location is not set correctly");
}

void TestInfoInstantiate() {
  TestDriverStub test_driver;
  bool called = false;
  TestInfo info(kTestName, kSourceLocation, [&called](TestDriver* driver) {
    auto test = zxtest::Test::Create<FakeTest>(driver);
    test->called_ = &called;
    return test;
  });

  std::unique_ptr<zxtest::Test> test = info.Instantiate(&test_driver);
  ZX_ASSERT_MSG(test != nullptr, "TestInfo::Instantiate returned nullptr.");
  test->Run();
  ZX_ASSERT_MSG(called, "TestInfo::Instantiate returned nullptr.");
}

}  // namespace test
}  // namespace zxtest
