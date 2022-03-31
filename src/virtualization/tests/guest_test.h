// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_GUEST_TEST_H_
#define SRC_VIRTUALIZATION_TESTS_GUEST_TEST_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/macros.h>

#include <optional>

#include <fbl/type_info.h>
#include <gtest/gtest.h>

#include "lib/zx/time.h"
#include "src/virtualization/tests/enclosed_guest.h"

// GuestTest creates a static EnclosedGuest to be shared across all tests in a
// test fixture.
template <class T>
class GuestTest : public ::testing::Test {
 public:
  static void SetUpTestSuite() {
    FX_LOGS(INFO) << "Guest: " << fbl::TypeInfo<T>::Name();
    loop_.emplace(&kAsyncLoopConfigAttachToCurrentThread);
    enclosed_guest_.emplace(*loop_);
    ASSERT_EQ(GetEnclosedGuest().Start(zx::time::infinite()), ZX_OK);
  }

  static void TearDownTestSuite() {
    EXPECT_EQ(GetEnclosedGuest().Stop(zx::time::infinite()), ZX_OK);
    loop_->Quit();
    enclosed_guest_.reset();
    loop_.reset();
  }

 protected:
  void SetUp() override {
    // An assertion failure in SetUpTestSuite doesn't prevent tests from running,
    // so we need to check that it succeeded here.
    ASSERT_TRUE(GetEnclosedGuest().Ready()) << "Guest setup failed";
  }

  zx_status_t Execute(const std::vector<std::string>& argv, std::string* result = nullptr,
                      int32_t* return_code = nullptr) {
    return GetEnclosedGuest().Execute(argv, {}, zx::time::infinite(), result, return_code);
  }

  zx_status_t Execute(const std::vector<std::string>& argv,
                      const std::unordered_map<std::string, std::string>& env,
                      std::string* result = nullptr, int32_t* return_code = nullptr) {
    return GetEnclosedGuest().Execute(argv, env, zx::time::infinite(), result, return_code);
  }

  zx_status_t RunUtil(const std::string& util, const std::vector<std::string>& argv,
                      std::string* result = nullptr) {
    return GetEnclosedGuest().RunUtil(util, argv, zx::time::infinite(), result);
  }

  GuestKernel GetGuestKernel() { return GetEnclosedGuest().GetGuestKernel(); }

  uint32_t GetGuestCid() { return GetEnclosedGuest().GetGuestCid(); }

  void GetHostVsockEndpoint(
      fidl::InterfaceRequest<fuchsia::virtualization::HostVsockEndpoint> endpoint) {
    GetEnclosedGuest().GetHostVsockEndpoint(std::move(endpoint));
  }

  void ConnectToBalloon(
      fidl::InterfaceRequest<fuchsia::virtualization::BalloonController> balloon_controller) {
    GetEnclosedGuest().ConnectToBalloon(std::move(balloon_controller));
  }

  static T& GetEnclosedGuest() { return enclosed_guest_.value(); }

 private:
  static inline std::optional<T> enclosed_guest_;
  static inline std::optional<async::Loop> loop_;
};

#endif  // SRC_VIRTUALIZATION_TESTS_GUEST_TEST_H_
