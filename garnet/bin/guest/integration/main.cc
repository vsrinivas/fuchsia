// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_printf.h>

#include "guest_test.h"
#include "logger.h"

using ::testing::HasSubstr;

static constexpr char kVirtioRngUtil[] = "virtio_rng_test_util";
static constexpr size_t kVirtioConsoleMessageCount = 100;

template <class T>
T* GuestTest<T>::enclosed_guest_ = nullptr;

class SingleCpuZirconEnclosedGuest : public ZirconEnclosedGuest {
  zx_status_t LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) override {
    launch_info->url = kZirconGuestUrl;
    launch_info->args.push_back("--virtio-gpu=false");
    launch_info->args.push_back("--cpus=1");
    launch_info->args.push_back("--cmdline-add=kernel.serial=none");
    return ZX_OK;
  }
};

class SingleCpuDebianEnclosedGuest : public DebianEnclosedGuest {
  zx_status_t LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) override {
    launch_info->url = kDebianGuestUrl;
    launch_info->args.push_back("--virtio-gpu=false");
    launch_info->args.push_back("--cpus=1");
    return ZX_OK;
  }
};

using GuestTypes =
    ::testing::Types<ZirconEnclosedGuest, SingleCpuZirconEnclosedGuest,
                     DebianEnclosedGuest, SingleCpuDebianEnclosedGuest>;
TYPED_TEST_SUITE(GuestTest, GuestTypes);

TYPED_TEST(GuestTest, LaunchGuest) {
  std::string result;
  EXPECT_EQ(this->Execute("echo \"test\"", &result), ZX_OK);
  EXPECT_EQ(result, "test\n");
}

TYPED_TEST(GuestTest, VirtioRng) {
  std::string result;
  EXPECT_EQ(this->RunUtil(kVirtioRngUtil, "", &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));
}

TYPED_TEST(GuestTest, VirtioConsole) {
  // Test many small packets.
  std::string result;
  for (size_t i = 0; i != kVirtioConsoleMessageCount; ++i) {
    EXPECT_EQ(this->Execute("echo \"test\"", &result), ZX_OK);
    EXPECT_EQ(result, "test\n");
  }

  // Test large packets. Note that we must keep the total length below 4096,
  // which is the maximum line length for dash.
  std::string test_data = "";
  for (size_t i = 0; i != kVirtioConsoleMessageCount; ++i) {
    test_data.append("Lorem ipsum dolor sit amet consectetur ");
  }
  std::string cmd = fxl::StringPrintf("echo \"%s\"", test_data.c_str());

  EXPECT_EQ(this->Execute(cmd, &result), ZX_OK);
  test_data.append("\n");
  EXPECT_EQ(result, test_data);
}

// This test event listener dumps the guest's serial logs when a test fails.
class LoggerOutputListener : public ::testing::EmptyTestEventListener {
  void OnTestEnd(const ::testing::TestInfo& info) override {
    if (!info.result()->Failed()) {
      return;
    }
    std::cout << "[----------] Begin guest output\n";
    std::cout << Logger::Get().Buffer();
    std::cout << "\n[----------] End guest output\n";
    std::cout.flush();
  }
};

int main(int argc, char** argv) {
  LoggerOutputListener listener;

  testing::InitGoogleTest(&argc, argv);
  testing::UnitTest::GetInstance()->listeners().Append(&listener);
  int status = RUN_ALL_TESTS();
  testing::UnitTest::GetInstance()->listeners().Release(&listener);

  return status;
}