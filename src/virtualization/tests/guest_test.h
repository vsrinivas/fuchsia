// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_GUEST_TEST_H_
#define SRC_VIRTUALIZATION_TESTS_GUEST_TEST_H_

#include <lib/syslog/cpp/macros.h>

#include <fbl/type_info.h>
#include <gtest/gtest.h>

#include "lib/zx/time.h"
#include "src/virtualization/tests/enclosed_guest.h"

// Timeout waiting for a guest to start / stop.
static constexpr zx::duration kGuestStartupTimeout = zx::sec(60);
static constexpr zx::duration kGuestShutdownTimeout = zx::sec(30);

// Timeout waiting for a single test case to run.
static constexpr zx::duration kPerTestTimeout = zx::sec(60);

// GuestTest creates a static EnclosedGuest to be shared across all tests in a
// test fixture.
template <class T>
class GuestTest : public ::testing::Test {
 public:
  static void SetUpTestSuite() {
    FX_LOGS(INFO) << "Guest: " << fbl::TypeInfo<T>::Name();
    enclosed_guest_ = new T();
    ASSERT_EQ(enclosed_guest_->Start(zx::deadline_after(kGuestStartupTimeout)), ZX_OK);
  }

  static void TearDownTestSuite() {
    EXPECT_EQ(enclosed_guest_->Stop(zx::deadline_after(kGuestShutdownTimeout)), ZX_OK);
    delete enclosed_guest_;
  }

 protected:
  void SetUp() override {
    // Set up the deadline for this test case.
    deadline_ = zx::deadline_after(kPerTestTimeout);

    // An assertion failure in SetUpTestSuite doesn't prevent tests from running,
    // so we need to check that it succeeded here.
    ASSERT_TRUE(enclosed_guest_->Ready()) << "Guest setup failed";
  }

  zx_status_t Execute(const std::vector<std::string>& argv, std::string* result = nullptr,
                      int32_t* return_code = nullptr) {
    return enclosed_guest_->Execute(argv, {}, deadline_, result, return_code);
  }

  zx_status_t Execute(const std::vector<std::string>& argv,
                      const std::unordered_map<std::string, std::string>& env,
                      std::string* result = nullptr, int32_t* return_code = nullptr) {
    return enclosed_guest_->Execute(argv, env, deadline_, result, return_code);
  }

  zx_status_t RunUtil(const std::string& util, const std::vector<std::string>& argv,
                      std::string* result = nullptr) {
    return enclosed_guest_->RunUtil(util, argv, deadline_, result);
  }

  GuestKernel GetGuestKernel() { return enclosed_guest_->GetGuestKernel(); }

  uint32_t GetGuestCid() { return enclosed_guest_->GetGuestCid(); }

  void GetHostVsockEndpoint(
      fidl::InterfaceRequest<fuchsia::virtualization::HostVsockEndpoint> endpoint) {
    enclosed_guest_->GetHostVsockEndpoint(std::move(endpoint));
  }

  void ConnectToBalloon(
      fidl::InterfaceRequest<fuchsia::virtualization::BalloonController> balloon_controller) {
    enclosed_guest_->ConnectToBalloon(std::move(balloon_controller));
  }

  T* GetEnclosedGuest() { return enclosed_guest_; }

  const T* GetEnclosedGuest() const { return enclosed_guest_; }

 private:
  zx::time deadline_;  // Deadline for a single test case.
  static inline T* enclosed_guest_ = nullptr;
};

#endif  // SRC_VIRTUALIZATION_TESTS_GUEST_TEST_H_
