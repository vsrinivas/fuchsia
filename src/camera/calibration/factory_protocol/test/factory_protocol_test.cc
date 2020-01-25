// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/calibration/factory_protocol/factory_protocol.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <gtest/gtest.h>
#include <src/lib/files/directory.h>
#include <src/lib/files/file.h>

namespace camera {
namespace {

const auto kDirPath = "/data/calibration";
const auto kFilename = "/frame_0.raw";
const auto kCameraVendorName = "Google Inc.";
const auto kCameraProductName = "Fuchsia Sherlock Camera";
const uint16_t kCameraVendorId = 0x18D1;
const uint16_t kCameraProductId = 0xF00D;
const uint8_t kStrLength = 17;

class FactoryProtocolTest : public testing::Test {
 public:
  FactoryProtocolTest() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}
  ~FactoryProtocolTest() override { loop_.Shutdown(); }

  void SetUp() override {
    thrd_t thread;
    ASSERT_EQ(ZX_OK, loop_.StartThread("factory_protocol_test_thread", &thread));

    auto channel = factory_protocol_.NewRequest(loop_.dispatcher()).TakeChannel();
    factory_protocol_.set_error_handler([&](zx_status_t status) {
      if (status != ZX_OK)
        ADD_FAILURE() << "Channel Failure: " << status;
    });

    factory_impl_ = FactoryProtocol::Create(std::move(channel), loop_.dispatcher());
    ASSERT_NE(nullptr, factory_impl_);
    ASSERT_FALSE(factory_impl_->streaming());
  }

  void TearDown() override {
    factory_impl_ = nullptr;
    factory_protocol_ = nullptr;
  }

  async::Loop loop_;
  std::unique_ptr<FactoryProtocol> factory_impl_;
  fuchsia::factory::camera::CameraFactoryPtr factory_protocol_;
};

TEST_F(FactoryProtocolTest, DISABLED_StreamingWritesToFile) {
  ASSERT_EQ(ZX_OK, factory_impl_->ConnectToStream());
  const std::string kDirPathStr(kDirPath, kStrLength);
  ASSERT_TRUE(files::IsDirectory(kDirPathStr));
  ASSERT_FALSE(factory_impl_->frames_received());

  while (!factory_impl_->frames_received() && !HasFailure()) {
    loop_.RunUntilIdle();
  }

  auto path = kDirPathStr + kFilename;
  ASSERT_TRUE(files::IsFile(path));
}

TEST_F(FactoryProtocolTest, DISABLED_ShutdownClosesChannelAndStream) {
  ASSERT_EQ(ZX_OK, factory_impl_->ConnectToStream());
  ASSERT_TRUE(factory_impl_->streaming());

  factory_impl_->Shutdown(ZX_OK);
  zx_status_t status =
      factory_protocol_.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr);
  if (status != ZX_OK) {
    EXPECT_EQ(status, ZX_ERR_BAD_HANDLE);
  }

  ASSERT_FALSE(factory_impl_->streaming());
}

TEST_F(FactoryProtocolTest, DISABLED_DetectCameraFIDL) {
  auto test_done = false;
  factory_protocol_->DetectCamera(
      [&test_done](fuchsia::factory::camera::CameraFactory_DetectCamera_Result result) {
        EXPECT_EQ(0, result.response().camera_id);
        auto device_info = std::move(result.response().camera_info);
        EXPECT_EQ(kCameraVendorName, device_info.vendor_name());
        EXPECT_EQ(kCameraVendorId, device_info.vendor_id());
        EXPECT_EQ(kCameraProductName, device_info.product_name());
        EXPECT_EQ(kCameraProductId, device_info.product_id());
        EXPECT_EQ(fuchsia::camera2::DeviceType::BUILTIN, device_info.type());
        test_done = true;
      });

  while (!test_done && !HasFailure())
    loop_.RunUntilIdle();
}

}  // namespace
}  // namespace camera
