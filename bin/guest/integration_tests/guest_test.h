// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_INTEGRATION_TESTS_GUEST_TEST_H_
#define GARNET_BIN_GUEST_INTEGRATION_TESTS_GUEST_TEST_H_

#include "garnet/bin/guest/integration_tests/enclosed_guest.h"

static constexpr char kZirconGuestUrl[] = "zircon_guest";
static constexpr char kLinuxGuestUrl[] = "linux_guest";

zx_status_t GuestWaitForSystemReady(EnclosedGuest& enclosed_guest);
zx_status_t GuestRun(EnclosedGuest& enclosed_guest, const std::string& cmx,
                     const std::string& args, std::string* result);

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

  static zx_status_t WaitForSystemReady() {
    return GuestWaitForSystemReady(*enclosed_guest_);
  }

  void SetUp() {
    // An assertion failure in SetUpTestCase doesn't prevent tests from running,
    // so we need to check that it succeeded here.
    ASSERT_TRUE(setup_succeeded_) << "Guest setup failed";
  }

  static zx_status_t Execute(const std::string& message,
                             std::string* result = nullptr) {
    return enclosed_guest_->Execute(message, result);
  }

  static zx_status_t Run(const std::string& cmx, const std::string& args,
                         std::string* result = nullptr) {
    return GuestRun(*enclosed_guest_, cmx, args, result);
  }

  uint32_t GetGuestCid() { return enclosed_guest_->GetGuestCid(); }

  void GetHostVsockEndpoint(
      fidl::InterfaceRequest<fuchsia::guest::HostVsockEndpoint> endpoint) {
    enclosed_guest_->GetHostVsockEndpoint(std::move(endpoint));
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