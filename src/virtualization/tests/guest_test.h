// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_GUEST_TEST_H_
#define SRC_VIRTUALIZATION_TESTS_GUEST_TEST_H_

#include <fbl/type_info.h>
#include <src/lib/fxl/logging.h>

#include "src/virtualization/tests/enclosed_guest.h"

// GuestTest creates a static EnclosedGuest to be shared across all tests in a
// test fixture. Each translation unit that uses GuestTest must instantiate
// enclosed_guest_ for each derivation of EnclosedGuest it uses. For example:
//    template <class T>
//    T* GuestTest<T>::enclosed_guest_ = nullptr;
template <class T>
class GuestTest : public ::testing::Test {
 public:
  static void SetUpTestCase() {
    FXL_LOG(INFO) << "Guest: " << fbl::TypeInfo<T>::Name();
    enclosed_guest_ = new T();
    ASSERT_EQ(enclosed_guest_->Start(), ZX_OK);
  }

  static void TearDownTestCase() {
    enclosed_guest_->Stop();
    delete enclosed_guest_;
  }

 protected:
  void SetUp() {
    // An assertion failure in SetUpTestCase doesn't prevent tests from running,
    // so we need to check that it succeeded here.
    ASSERT_TRUE(enclosed_guest_->Ready()) << "Guest setup failed";
  }

  static zx_status_t Execute(const std::string& message,
                             std::string* result = nullptr) {
    return enclosed_guest_->Execute(message, result);
  }

  static zx_status_t RunUtil(const std::string& util,
                             const std::vector<std::string>& argv,
                             std::string* result = nullptr) {
    return enclosed_guest_->RunUtil(util, argv, result);
  }

  GuestKernel GetGuestKernel() { return enclosed_guest_->GetGuestKernel(); }

  uint32_t GetGuestCid() { return enclosed_guest_->GetGuestCid(); }

  void GetHostVsockEndpoint(
      fidl::InterfaceRequest<fuchsia::virtualization::HostVsockEndpoint>
          endpoint) {
    enclosed_guest_->GetHostVsockEndpoint(std::move(endpoint));
  }

  void ConnectToBalloon(
      fidl::InterfaceRequest<fuchsia::virtualization::BalloonController>
          balloon_controller) {
    enclosed_guest_->ConnectToBalloon(std::move(balloon_controller));
  }

  T* GetEnclosedGuest() { return enclosed_guest_; }

  const T* GetEnclosedGuest() const { return enclosed_guest_; }

 private:
  static T* enclosed_guest_;
};

#endif  // SRC_VIRTUALIZATION_TESTS_GUEST_TEST_H_
