// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <stdio.h>

#include "runner.h"
#include "test.h"

namespace display_test {

struct test_info {
  const char* name;
  Test test;
};
std::vector<struct test_info>* tests = nullptr;

bool RegisterTest(const char* name, Test fn) {
  if (!tests) {
    tests = new std::vector<struct test_info>();
  }
  tests->push_back({.name = name, .test = std::move(fn)});
  return true;
}

}  // namespace display_test

int main(int argc, char** argv) {
  if (argc != 2) {
    printf("Usage: display_capture_test <monitor name>\n");
    return -1;
  }
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  display_test::internal::Runner runner(&loop);

  zx_status_t status = runner.Start(argv[1]);
  ZX_ASSERT(status == ZX_OK);

  uint32_t pass_count = 0;
  uint32_t skip_count = 0;
  uint32_t fail_count = 0;
  zx_time_t start_time = zx_clock_get_monotonic();
  auto iter = display_test::tests->begin();
  while (iter != display_test::tests->end()) {
    auto& test = *iter;

    printf("Running test %s\n", test.name);
    auto ctx = runner.StartTest();
    test.test(ctx);
    ZX_ASSERT(ctx->has_frame());
    runner.OnResourceReady();

    loop.ResetQuit();
    loop.Run();

    int32_t status = runner.CleanupTest();
    switch (status) {
      case runner.kTestOk:
        printf("------- PASS\n");
        pass_count++;
        break;
      case runner.kTestDisplayCheckFail:
        printf("------- SKIP\n");
        skip_count++;
        break;
      case runner.kTestVsyncFail:
      case runner.kTestCaptureMismatch:
        printf("------- FAIL (%d)\n", status);
        fail_count++;
        break;
      case runner.kTestCaptureFail:
        ZX_ASSERT_MSG(false, "Display capture failed");
      case runner.kTestStatusUnknown:
        ZX_ASSERT_MSG(false, "Test runner failure");
      default:
        ZX_ASSERT_MSG(false, "Unknown test result");
    }

    iter++;
  }

  runner.Stop();

  printf("Test took %ld ms\n",
         (zx_clock_get_monotonic() - start_time) / 1000000);
  printf("Pass: %d Skip: %d Fail: %d\n", pass_count, skip_count, fail_count);
  return fail_count;
}
