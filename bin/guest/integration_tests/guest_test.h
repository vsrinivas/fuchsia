// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_INTEGRATION_TESTS_GUEST_TEST_H_
#define GARNET_BIN_GUEST_INTEGRATION_TESTS_GUEST_TEST_H_

#include "garnet/bin/guest/integration_tests/enclosed_guest.h"

static constexpr char kZirconGuestUrl[] = "zircon_guest";
static constexpr char kLinuxGuestUrl[] = "linux_guest";
static constexpr char kTestUtilsUrl[] =
    "fuchsia-pkg://fuchsia.com/guest_integration_tests_utils";

template <class T>
class GuestTest : public ::testing::Test {
 protected:
  static void SetUpTestCase() {
    enclosed_guest_ = new EnclosedGuest();
    fuchsia::guest::LaunchInfo launch_info;
    ASSERT_TRUE(T::LaunchInfo(&launch_info));
    ASSERT_EQ(enclosed_guest_->Start(std::move(launch_info)), ZX_OK);
    ASSERT_TRUE(T::SetUpGuest());
    setup_succeeded_ = true;
  }

  static bool SetUpGuest() { return true; }

  static void TearDownTestCase() {
    enclosed_guest_->Stop();
    delete enclosed_guest_;
  }

  static zx_status_t WaitForPkgfs() {
    for (size_t i = 0; i != 20; ++i) {
      std::string ps;
      zx_status_t status = Execute("ps", &ps);
      if (status != ZX_OK) {
        return status;
      }
      zx::nanosleep(zx::deadline_after(zx::msec(500)));
      auto pkgfs = ps.find("pkgfs");
      if (pkgfs != std::string::npos) {
        return ZX_OK;
      }
    }
    return ZX_ERR_BAD_STATE;
  }

  void SetUp() {
    // An assertion failure in SetUpTestCase doesn't prevent tests from running,
    // so we need to check that it succeeded here.
    ASSERT_TRUE(setup_succeeded_) << "Guest setup failed";
  }

  static zx_status_t Execute(std::string message,
                             std::string* result = nullptr) {
    return enclosed_guest_->Execute(message, result);
  }

 private:
  static bool setup_succeeded_;
  static EnclosedGuest* enclosed_guest_;
};

template <class T>
bool GuestTest<T>::setup_succeeded_ = false;
template <class T>
EnclosedGuest* GuestTest<T>::enclosed_guest_ = nullptr;

#endif  // GARNET_BIN_GUEST_INTEGRATION_TESTS_GUEST_TEST_H_