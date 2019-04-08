// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_printf.h>
#include <string.h>
#include <virtio/balloon.h>

#include "guest_test.h"
#include "logger.h"

using ::testing::HasSubstr;

static constexpr char kVirtioRngUtil[] = "virtio_rng_test_util";
static constexpr size_t kVirtioConsoleMessageCount = 100;
static constexpr size_t kVirtioBalloonPageCount = 256;

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

using VirtioBalloonGuestTest = GuestTest<DebianEnclosedGuest>;

TEST_F(VirtioBalloonGuestTest, VirtioBalloon) {
  std::string result;
  EXPECT_EQ(this->Execute("echo \"test\"", &result), ZX_OK);
  EXPECT_EQ(result, "test\n");

  fuchsia::guest::BalloonControllerSyncPtr balloon_controller;
  ConnectToBalloon(balloon_controller.NewRequest());

  uint32_t initial_num_pages;
  zx_status_t status = balloon_controller->GetNumPages(&initial_num_pages);
  ASSERT_EQ(status, ZX_OK);

  // Request an increase to the number of pages in the balloon.
  status = balloon_controller->RequestNumPages(initial_num_pages +
                                               kVirtioBalloonPageCount);
  ASSERT_EQ(status, ZX_OK);

  // Verify that the number of pages eventually equals the requested number. The
  // guest may not respond to the request immediately so we call GetNumPages in
  // a loop.
  uint32_t num_pages;
  while (true) {
    status = balloon_controller->GetNumPages(&num_pages);
    ASSERT_EQ(status, ZX_OK);
    if (num_pages == initial_num_pages + kVirtioBalloonPageCount) {
      break;
    }
  }

  // Request a decrease to the number of pages in the balloon back to the
  // initial value.
  status = balloon_controller->RequestNumPages(initial_num_pages);
  ASSERT_EQ(status, ZX_OK);

  while (true) {
    status = balloon_controller->GetNumPages(&num_pages);
    ASSERT_EQ(status, ZX_OK);
    if (num_pages == initial_num_pages) {
      break;
    }
  }
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