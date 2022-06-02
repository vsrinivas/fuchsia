// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_GUEST_TEST_H_
#define SRC_VIRTUALIZATION_TESTS_GUEST_TEST_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

#include <fbl/type_info.h>
#include <gtest/gtest.h>

#include "lib/zx/time.h"
#include "src/virtualization/tests/enclosed_guest.h"

template <class T>
class GuestTest : public ::testing::Test {
 protected:
  void SetUp() override {
    FX_LOGS(INFO) << "Guest: " << fbl::TypeInfo<T>::Name();
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
    enclosed_guest_ = std::make_unique<T>(*loop_);
    ZX_ASSERT(enclosed_guest_->Start(zx::time::infinite()) == ZX_OK);
  }

  void TearDown() override {
    FX_LOGS(INFO) << "Teardown Guest: " << fbl::TypeInfo<T>::Name();
    ZX_ASSERT(enclosed_guest_->Stop(zx::time::infinite()) == ZX_OK);
    loop_->Quit();
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

  T& GetEnclosedGuest() const { return *enclosed_guest_; }

 private:
  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<T> enclosed_guest_;
};

#endif  // SRC_VIRTUALIZATION_TESTS_GUEST_TEST_H_
