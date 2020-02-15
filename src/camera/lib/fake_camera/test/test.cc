// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/camera/lib/fake_camera/fake_camera.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

constexpr auto kCameraIdentifier = "FakeCameraTest";

class FakeCameraTest : public gtest::RealLoopFixture {
 public:
  FakeCameraTest() { context_ = sys::ComponentContext::Create(); }

 protected:
  virtual void SetUp() override {}
  virtual void TearDown() override {}

  template <class T>
  static void SetFailOnError(fidl::InterfacePtr<T>& ptr, std::string name = T::Name_) {
    ptr.set_error_handler([=](zx_status_t status) {
      ADD_FAILURE() << name << " server disconnected: " << zx_status_get_string(status);
    });
  }

  std::unique_ptr<sys::ComponentContext> context_;
};

TEST_F(FakeCameraTest, InvalidArgs) {
  {  // No configurations.
    auto result = camera::FakeCamera::Create(kCameraIdentifier, {});
    ASSERT_TRUE(result.is_error());
    EXPECT_EQ(result.error(), ZX_ERR_INVALID_ARGS);
  }

  {  // No streams.
    std::vector<camera::FakeConfiguration> configs;
    configs.push_back({});
    auto result = camera::FakeCamera::Create(kCameraIdentifier, std::move(configs));
    ASSERT_TRUE(result.is_error());
    EXPECT_EQ(result.error(), ZX_ERR_INVALID_ARGS);
  }

  {  // Null stream.
    camera::FakeConfiguration::AttachedFakeStream stream;
    camera::FakeConfiguration config;
    config.streams.push_back(std::move(stream));
    std::vector<camera::FakeConfiguration> configs;
    configs.push_back(std::move(config));
    auto result = camera::FakeCamera::Create(kCameraIdentifier, std::move(configs));
    ASSERT_TRUE(result.is_error());
    EXPECT_EQ(result.error(), ZX_ERR_INVALID_ARGS);
  }
}

TEST_F(FakeCameraTest, ConnectTokenReceived) {
  fuchsia::camera3::StreamProperties properties{
      .image_format{.pixel_format{.type = fuchsia::sysmem::PixelFormatType::NV12},
                    .coded_width = 256,
                    .coded_height = 128,
                    .bytes_per_row = 256,
                    .color_space{.type = fuchsia::sysmem::ColorSpaceType::REC601_NTSC}},
      .frame_rate{
          .numerator = 30,
          .denominator = 1,
      },
      .supported_resolutions{{.coded_size{.width = 256, .height = 128}, .bytes_per_row = 256}}};
  auto stream_result = camera::FakeStream::Create(std::move(properties));
  ASSERT_TRUE(stream_result.is_ok());
  std::shared_ptr<camera::FakeStream> stream = stream_result.take_value();

  bool callback_invoked = false;
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token_received;
  camera::FakeConfiguration::AttachedFakeStream afs{
      .stream = stream,
      .connection_callback =
          [&](fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
            token_received = std::move(token);
            callback_invoked = true;
          }};
  camera::FakeConfiguration config;
  config.streams.push_back(std::move(afs));
  std::vector<camera::FakeConfiguration> configs;
  configs.push_back(std::move(config));
  auto camera_result = camera::FakeCamera::Create(kCameraIdentifier, std::move(configs));
  ASSERT_TRUE(camera_result.is_ok());
  auto camera = camera_result.take_value();

  fuchsia::sysmem::AllocatorPtr allocator;
  SetFailOnError(allocator);
  fuchsia::camera3::DevicePtr device_protocol;
  SetFailOnError(device_protocol, "Device");
  fuchsia::camera3::StreamPtr stream_protocol;
  SetFailOnError(stream_protocol, "Stream");
  context_->svc()->Connect(allocator.NewRequest());
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token_sent;
  allocator->AllocateSharedCollection(token_sent.NewRequest());
  auto token_sent_handle = token_sent.channel().get();
  camera->GetHandler()(device_protocol.NewRequest());
  device_protocol->GetConfigurations(
      [&](std::vector<fuchsia::camera3::Configuration> configurations) {
        ASSERT_FALSE(configurations.empty());
        ASSERT_FALSE(configurations[0].streams.empty());
        device_protocol->ConnectToStream(0, std::move(token_sent), stream_protocol.NewRequest());
      });
  RunLoopUntil([&]() { return HasFailure() || callback_invoked; });
  EXPECT_EQ(token_received.channel().get(), token_sent_handle);
  token_received.Bind()->Close();
}
