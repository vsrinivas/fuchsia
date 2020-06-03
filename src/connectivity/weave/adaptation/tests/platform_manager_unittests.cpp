// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/PlatformManager.h>
#include "src/connectivity/weave/adaptation/platform_manager_impl.h"
#include "weave_test_fixture.h"

namespace nl::Weave::DeviceLayer::Internal {
namespace testing {
namespace {
using nl::Weave::DeviceLayer::PlatformManager;
using nl::Weave::DeviceLayer::PlatformManagerImpl;
}

class PlatformManagerTest : public WeaveTestFixture {
 public:
  void SetUp() {
    WeaveTestFixture::SetUp();
    PlatformMgrImpl().SetDispatcher(dispatcher());
  }

  void TearDown() {
    WeaveTestFixture::TearDown();
  }
};

class TestAppend {
 public:
  std::string *append_to_;
  const char* str_;
  TestAppend(std::string *append_str, const char* s)
    : append_to_(append_str), str_(s) {}
  static void AppendStr(intptr_t arg) {
    TestAppend* t = reinterpret_cast<TestAppend*>(arg);
    t->append_to_->append(t->str_);
  }
};

TEST_F(PlatformManagerTest, ScheduleMultipleWork) {
  const char *test1 = "abc";
  const char *test2 = "def";
  std::string s = "abcdef";
  std::string append_to;
  TestAppend t1(&append_to, test1);
  TestAppend t2(&append_to, test2);
  PlatformMgr().ScheduleWork(TestAppend::AppendStr, (intptr_t)&t1);
  PlatformMgr().ScheduleWork(TestAppend::AppendStr, (intptr_t)&t2);
  RunLoopUntilIdle();
  EXPECT_STREQ(s.c_str(), append_to.c_str());
}

}   // namespace testing
}  // namespace nl::Weave::DeviceLayer::Internal
