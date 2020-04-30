// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/errors.h>

#include <limits>

#include "src/camera/bin/device/device_impl.h"
#include "src/camera/bin/device/stream_impl.h"
#include "src/camera/bin/device/test/fake_controller.h"
#include "src/camera/bin/device/util.h"
#include "src/camera/lib/fake_legacy_stream/fake_legacy_stream.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace camera {

// No-op function.
static void nop() {}

static void nop_stream_requested(
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
    fidl::InterfaceRequest<fuchsia::camera2::Stream> request,
    fit::function<void(uint32_t)> callback, uint32_t format_index) {
  token.Bind()->Close();
  request.Close(ZX_ERR_NOT_SUPPORTED);
  callback(0);
}

class DeviceTest : public gtest::RealLoopFixture {
 protected:
  DeviceTest() : context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()) {}

  void SetUp() override {
    context_->svc()->Connect(allocator_.NewRequest());
    allocator_.set_error_handler(MakeErrorHandler("Sysmem Allocator"));

    fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> controller;
    auto controller_result = FakeController::Create(controller.NewRequest());
    ASSERT_TRUE(controller_result.is_ok());
    controller_ = controller_result.take_value();

    fidl::InterfaceHandle<fuchsia::sysmem::Allocator> allocator;
    context_->svc()->Connect(allocator.NewRequest());

    auto device_result = DeviceImpl::Create(std::move(controller), std::move(allocator));
    ASSERT_TRUE(device_result.is_ok());
    device_ = device_result.take_value();
  }

  void TearDown() override {
    device_ = nullptr;
    controller_ = nullptr;
    allocator_ = nullptr;
    RunLoopUntilIdle();
  }

  static fit::function<void(zx_status_t status)> MakeErrorHandler(std::string server) {
    return [server](zx_status_t status) {
      ADD_FAILURE() << server << " server disconnected - " << status;
    };
  }

  template <class T>
  static void SetFailOnError(fidl::InterfacePtr<T>& ptr, std::string name = T::Name_) {
    ptr.set_error_handler([=](zx_status_t status) {
      ADD_FAILURE() << name << " server disconnected: " << zx_status_get_string(status);
    });
  }

  void RunLoopUntilFailureOr(bool& condition) {
    RunLoopUntil([&]() { return HasFailure() || condition; });
  }

  // Synchronizes messages to a device. This method returns when an error occurs or all messages
  // sent to |device| have been received by the server.
  void Sync(fuchsia::camera3::DevicePtr& device) {
    bool identifier_returned = false;
    device->GetIdentifier([&](fidl::StringPtr identifier) { identifier_returned = true; });
    RunLoopUntilFailureOr(identifier_returned);
  }

  // Synchronizes messages to a stream. This method returns when an error occurs or all messages
  // sent to |stream| have been received by the server.
  void Sync(fuchsia::camera3::StreamPtr& stream) {
    fuchsia::camera3::StreamPtr stream2;
    SetFailOnError(stream2, "Rebound Stream for DeviceTest::Sync");
    stream->Rebind(stream2.NewRequest());
    bool resolution_returned = false;
    stream2->WatchResolution([&](fuchsia::math::Size resolution) { resolution_returned = true; });
    RunLoopUntilFailureOr(resolution_returned);
  }

  std::unique_ptr<sys::ComponentContext> context_;
  std::unique_ptr<DeviceImpl> device_;
  std::unique_ptr<FakeController> controller_;
  fuchsia::sysmem::AllocatorPtr allocator_;
  fuchsia::camera3::StreamProperties fake_properties_;
  fuchsia::camera2::hal::StreamConfig fake_legacy_config_;
};

TEST_F(DeviceTest, CreateStreamNullConnection) {
  StreamImpl stream(fake_properties_, fake_legacy_config_, nullptr, nop_stream_requested, nop);
}

TEST_F(DeviceTest, CreateStreamFakeLegacyStream) {
  fidl::InterfaceHandle<fuchsia::camera3::Stream> stream;
  StreamImpl stream_impl(
      fake_properties_, fake_legacy_config_, stream.NewRequest(),
      [](fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
         fidl::InterfaceRequest<fuchsia::camera2::Stream> request,
         fit::function<void(uint32_t)> callback, uint32_t format_index) {
        token.Bind()->Close();
        auto result = FakeLegacyStream::Create(std::move(request));
        ASSERT_TRUE(result.is_ok());
        callback(0);
      },
      nop);
  RunLoopUntilIdle();
}

TEST_F(DeviceTest, ConvertConfig) {
  auto configs = FakeController::GetDefaultConfigs();
  ASSERT_FALSE(configs.empty());
  auto& a = configs[0];
  ASSERT_FALSE(a.stream_configs.empty());
  ASSERT_FALSE(a.stream_configs[0].image_formats.empty());
  auto result = Convert(a);
  ASSERT_TRUE(result.is_ok());
  auto b = result.take_value();
  EXPECT_EQ(a.stream_configs.size(), b.streams.size());
}

TEST_F(DeviceTest, GetFrames) {
  fuchsia::camera3::StreamPtr stream;
  stream.set_error_handler(MakeErrorHandler("Stream"));
  constexpr uint32_t kBufferId1 = 42;
  constexpr uint32_t kBufferId2 = 17;
  std::unique_ptr<FakeLegacyStream> legacy_stream_fake;
  bool legacy_stream_created = false;
  auto stream_impl = std::make_unique<StreamImpl>(
      fake_properties_, fake_legacy_config_, stream.NewRequest(),
      [&](fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
          fidl::InterfaceRequest<fuchsia::camera2::Stream> request,
          fit::function<void(uint32_t)> callback, uint32_t format_index) {
        auto result = FakeLegacyStream::Create(std::move(request));
        ASSERT_TRUE(result.is_ok());
        legacy_stream_fake = result.take_value();
        token.BindSync()->Close();
        legacy_stream_created = true;
        callback(1);
      },
      nop);

  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;
  allocator_->AllocateSharedCollection(token.NewRequest());
  stream->SetBufferCollection(std::move(token));
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> received_token;
  bool buffer_collection_returned = false;
  stream->WatchBufferCollection(
      [&](fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
        received_token = std::move(token);
        buffer_collection_returned = true;
      });

  RunLoopUntil(
      [&]() { return HasFailure() || (legacy_stream_created && buffer_collection_returned); });
  ASSERT_FALSE(HasFailure());

  fuchsia::sysmem::BufferCollectionPtr collection;
  collection.set_error_handler(MakeErrorHandler("Buffer Collection"));
  allocator_->BindSharedCollection(std::move(received_token), collection.NewRequest());
  collection->SetConstraints(
      true, {.usage{.cpu = fuchsia::sysmem::cpuUsageRead},
             .min_buffer_count_for_camping = 2,
             .image_format_constraints_count = 1,
             .image_format_constraints{
                 {{.pixel_format{.type = fuchsia::sysmem::PixelFormatType::NV12},
                   .color_spaces_count = 1,
                   .color_space{{{.type = fuchsia::sysmem::ColorSpaceType::REC601_NTSC}}},
                   .min_coded_width = 1,
                   .min_coded_height = 1}}}});
  bool buffers_allocated_returned = false;
  collection->WaitForBuffersAllocated(
      [&](zx_status_t status, fuchsia::sysmem::BufferCollectionInfo_2 buffers) {
        EXPECT_EQ(status, ZX_OK);
        buffers_allocated_returned = true;
      });
  RunLoopUntil([&]() { return HasFailure() || buffers_allocated_returned; });
  ASSERT_FALSE(HasFailure());

  bool frame1_received = false;
  bool frame2_received = false;
  auto callback2 = [&](fuchsia::camera3::FrameInfo info) {
    ASSERT_EQ(info.buffer_index, kBufferId2);
    frame2_received = true;
  };
  auto callback1 = [&](fuchsia::camera3::FrameInfo info) {
    ASSERT_EQ(info.buffer_index, kBufferId1);
    frame1_received = true;
    info.release_fence.reset();
    fuchsia::camera2::FrameAvailableInfo frame2_info;
    frame2_info.frame_status = fuchsia::camera2::FrameStatus::OK;
    frame2_info.buffer_id = kBufferId2;
    frame2_info.metadata.set_timestamp(0);
    ASSERT_EQ(legacy_stream_fake->SendFrameAvailable(std::move(frame2_info)), ZX_OK);
    stream->GetNextFrame(std::move(callback2));
  };
  stream->GetNextFrame(std::move(callback1));
  fuchsia::camera2::FrameAvailableInfo frame1_info;
  frame1_info.frame_status = fuchsia::camera2::FrameStatus::OK;
  frame1_info.buffer_id = kBufferId1;
  frame1_info.metadata.set_timestamp(0);
  ASSERT_EQ(legacy_stream_fake->SendFrameAvailable(std::move(frame1_info)), ZX_OK);
  while (!HasFailure() && (!frame1_received || !frame2_received)) {
    RunLoopUntilIdle();
  }
  auto client_result = legacy_stream_fake->StreamClientStatus();
  EXPECT_TRUE(client_result.is_ok()) << client_result.error();
  stream = nullptr;
  stream_impl = nullptr;
}

TEST_F(DeviceTest, GetFramesInvalidCall) {
  bool stream_errored = false;
  fuchsia::camera3::StreamPtr stream;
  stream.set_error_handler([&](zx_status_t status) {
    EXPECT_EQ(status, ZX_ERR_BAD_STATE);
    stream_errored = true;
  });
  std::unique_ptr<FakeLegacyStream> fake_legacy_stream;
  auto stream_impl = std::make_unique<StreamImpl>(
      fake_properties_, fake_legacy_config_, stream.NewRequest(),
      [&](fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
          fidl::InterfaceRequest<fuchsia::camera2::Stream> request,
          fit::function<void(uint32_t)> callback, uint32_t format_index) {
        auto result = FakeLegacyStream::Create(std::move(request));
        ASSERT_TRUE(result.is_ok());
        fake_legacy_stream = result.take_value();
        token.BindSync()->Close();
        callback(0);
      },
      nop);
  stream->GetNextFrame([](fuchsia::camera3::FrameInfo info) {});
  stream->GetNextFrame([](fuchsia::camera3::FrameInfo info) {});
  while (!HasFailure() && !stream_errored) {
    RunLoopUntilIdle();
  }
}

TEST_F(DeviceTest, Configurations) {
  fuchsia::camera3::DevicePtr device;
  SetFailOnError(device, "Device");
  device_->GetHandler()(device.NewRequest());

  uint32_t callback_count = 0;
  constexpr uint32_t kExpectedCallbackCount = 3;
  bool all_callbacks_received = false;
  device->GetConfigurations([&](std::vector<fuchsia::camera3::Configuration> configurations) {
    EXPECT_GE(configurations.size(), 2u);
    all_callbacks_received = ++callback_count == kExpectedCallbackCount;
  });
  device->SetCurrentConfiguration(0);
  RunLoopUntilIdle();
  device->WatchCurrentConfiguration([&](uint32_t index) {
    EXPECT_EQ(index, 0u);
    all_callbacks_received = ++callback_count == kExpectedCallbackCount;
    device->WatchCurrentConfiguration([&](uint32_t index) {
      EXPECT_EQ(index, 1u);
      all_callbacks_received = ++callback_count == kExpectedCallbackCount;
    });
    RunLoopUntilIdle();
    device->SetCurrentConfiguration(1);
  });
  RunLoopUntilFailureOr(all_callbacks_received);
}

TEST_F(DeviceTest, Identifier) {
  fuchsia::camera3::DevicePtr device;
  SetFailOnError(device, "Device");
  device_->GetHandler()(device.NewRequest());
  bool callback_received = false;
  device->GetIdentifier([&](fidl::StringPtr identifier) {
    ASSERT_TRUE(identifier.has_value());
    constexpr auto kExpectedDeviceIdentifier = "FFFF0ABC";
    EXPECT_EQ(identifier.value(), kExpectedDeviceIdentifier);
    callback_received = true;
  });
  RunLoopUntilFailureOr(callback_received);
}

TEST_F(DeviceTest, RequestStreamFromController) {
  fuchsia::camera3::DevicePtr device;
  SetFailOnError(device, "Device");
  device_->GetHandler()(device.NewRequest());
  fuchsia::camera3::StreamPtr stream;
  SetFailOnError(stream, "Stream");
  device->ConnectToStream(0, stream.NewRequest());
  fuchsia::sysmem::BufferCollectionTokenPtr token;
  allocator_->AllocateSharedCollection(token.NewRequest());
  token->Sync([&]() { stream->SetBufferCollection(std::move(token)); });
  stream->WatchBufferCollection(
      [&](fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
        fuchsia::sysmem::BufferCollectionPtr buffers;
        SetFailOnError(buffers, "BufferCollection");
        allocator_->BindSharedCollection(std::move(token), buffers.NewRequest());
        buffers->SetConstraints(true, {.usage{.cpu = fuchsia::sysmem::cpuUsageRead},
                                       .min_buffer_count_for_camping = 1});
        buffers->Close();
      });
  constexpr uint32_t kBufferId = 42;
  bool callback_received = false;
  stream->GetNextFrame([&](fuchsia::camera3::FrameInfo info) {
    EXPECT_EQ(info.buffer_index, kBufferId);
    callback_received = true;
  });
  bool frame_sent = false;
  while (!HasFailure() && !frame_sent) {
    RunLoopUntilIdle();
    fuchsia::camera2::FrameAvailableInfo info;
    info.frame_status = fuchsia::camera2::FrameStatus::OK;
    info.buffer_id = kBufferId;
    info.metadata.set_timestamp(0);
    zx_status_t status = controller_->SendFrameViaLegacyStream(std::move(info));
    if (status == ZX_OK) {
      frame_sent = true;
    } else {
      EXPECT_EQ(status, ZX_ERR_SHOULD_WAIT);
    }
  }
  RunLoopUntilFailureOr(callback_received);
}

TEST_F(DeviceTest, DeviceClientDisconnect) {
  // Create the first client.
  fuchsia::camera3::DevicePtr device;
  SetFailOnError(device, "Device");
  device_->GetHandler()(device.NewRequest());

  // Try to connect a second client, which should fail.
  fuchsia::camera3::DevicePtr device2;
  bool error_received = false;
  device2.set_error_handler([&](zx_status_t status) {
    EXPECT_EQ(status, ZX_ERR_ALREADY_BOUND);
    error_received = true;
  });
  device_->GetHandler()(device2.NewRequest());
  RunLoopUntilFailureOr(error_received);

  // Disconnect the first client, then try to connect the second again.
  device = nullptr;
  bool callback_received = false;
  while (!HasFailure() && !callback_received) {
    error_received = false;
    device_->GetHandler()(device2.NewRequest());
    // Call a returning API to verify the connection status.
    device2->GetIdentifier([&](fidl::StringPtr identifier) { callback_received = true; });
    RunLoopUntil([&] { return error_received || callback_received; });
  }

  RunLoopUntilFailureOr(callback_received);
}

TEST_F(DeviceTest, StreamClientDisconnect) {
  // Create the first client.
  fuchsia::camera3::DevicePtr device;
  SetFailOnError(device, "Device");
  device_->GetHandler()(device.NewRequest());
  fuchsia::camera3::StreamPtr stream;
  device->ConnectToStream(0, stream.NewRequest());
  SetFailOnError(stream, "Stream");

  // Try to connect a second client, which should fail.
  fuchsia::camera3::StreamPtr stream2;
  bool error_received = false;
  stream2.set_error_handler([&](zx_status_t status) {
    EXPECT_EQ(status, ZX_ERR_ALREADY_BOUND);
    error_received = true;
  });
  device->ConnectToStream(0, stream2.NewRequest());
  RunLoopUntilFailureOr(error_received);

  // Disconnect the first client, then try to connect the second again.
  stream = nullptr;
  bool callback_received = false;
  while (!HasFailure() && !callback_received) {
    error_received = false;
    device->ConnectToStream(0, stream2.NewRequest());
    fuchsia::sysmem::BufferCollectionTokenPtr token;
    SetFailOnError(token, "Token");
    allocator_->AllocateSharedCollection(token.NewRequest());
    token->Sync([&, token = std::move(token)]() mutable {
      stream2->SetBufferCollection(std::move(token));
    });
    // Call a returning API to verify the connection status.
    stream2->WatchBufferCollection([&](fuchsia::sysmem::BufferCollectionTokenHandle token) {
      EXPECT_EQ(token.BindSync()->Close(), ZX_OK);
      callback_received = true;
    });
    RunLoopUntil([&] { return error_received || callback_received; });
  }
}

TEST_F(DeviceTest, SetResolution) {
  fuchsia::camera3::DevicePtr device;
  SetFailOnError(device, "Device");
  device_->GetHandler()(device.NewRequest());
  fuchsia::camera3::StreamPtr stream;
  device->ConnectToStream(0, stream.NewRequest());
  SetFailOnError(stream, "Stream");
  constexpr fuchsia::math::Size kExpectedDefaultSize{.width = 1920, .height = 1080};
  constexpr fuchsia::math::Size kRequestedSize{.width = 1025, .height = 32};
  constexpr fuchsia::math::Size kExpectedSize{.width = 1280, .height = 720};
  constexpr fuchsia::math::Size kRequestedSize2{.width = 1, .height = 1};
  constexpr fuchsia::math::Size kExpectedSize2{.width = 1024, .height = 576};
  bool callback_received = false;
  stream->WatchResolution([&](fuchsia::math::Size coded_size) {
    EXPECT_GE(coded_size.width, kExpectedDefaultSize.width);
    EXPECT_GE(coded_size.height, kExpectedDefaultSize.height);
    callback_received = true;
  });
  RunLoopUntilFailureOr(callback_received);
  stream->SetResolution(kRequestedSize);
  callback_received = false;
  stream->WatchResolution([&](fuchsia::math::Size coded_size) {
    EXPECT_GE(coded_size.width, kExpectedSize.width);
    EXPECT_GE(coded_size.height, kExpectedSize.height);
    callback_received = true;
  });
  RunLoopUntilFailureOr(callback_received);
  callback_received = false;
  stream->SetResolution(kRequestedSize2);
  stream->WatchResolution([&](fuchsia::math::Size coded_size) {
    EXPECT_GE(coded_size.width, kExpectedSize2.width);
    EXPECT_GE(coded_size.height, kExpectedSize2.height);
    callback_received = true;
  });
  RunLoopUntilFailureOr(callback_received);
}

TEST_F(DeviceTest, SetResolutionInvalid) {
  fuchsia::camera3::DevicePtr device;
  SetFailOnError(device, "Device");
  device_->GetHandler()(device.NewRequest());
  fuchsia::camera3::StreamPtr stream;
  device->ConnectToStream(0, stream.NewRequest());
  constexpr fuchsia::math::Size kSize{.width = std::numeric_limits<int32_t>::max(), .height = 42};
  stream->SetResolution(kSize);
  bool error_received = false;
  stream.set_error_handler([&](zx_status_t status) {
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    error_received = true;
  });
  RunLoopUntilFailureOr(error_received);
}

TEST_F(DeviceTest, SetConfigurationDisconnectsStreams) {
  fuchsia::camera3::DevicePtr device;
  SetFailOnError(device, "Device");
  device_->GetHandler()(device.NewRequest());
  fuchsia::camera3::StreamPtr stream;
  bool error_received = false;
  stream.set_error_handler([&](zx_status_t status) {
    EXPECT_EQ(status, ZX_ERR_PEER_CLOSED);
    error_received = true;
  });
  device->ConnectToStream(0, stream.NewRequest());
  Sync(stream);
  device->SetCurrentConfiguration(0);
  RunLoopUntilFailureOr(error_received);
}

TEST_F(DeviceTest, Rebind) {
  // First device connection.
  fuchsia::camera3::DevicePtr device;
  SetFailOnError(device, "Device");
  device_->GetHandler()(device.NewRequest());

  // First stream connection.
  fuchsia::camera3::StreamPtr stream;
  SetFailOnError(stream, "Stream");
  device->ConnectToStream(0, stream.NewRequest());
  Sync(stream);

  // Rebind second device connection.
  fuchsia::camera3::DevicePtr device2;
  SetFailOnError(device2, "Device");
  device->Rebind(device2.NewRequest());

  // Attempt to bind second stream independently.
  fuchsia::camera3::StreamPtr stream2;
  bool error_received = false;
  stream2.set_error_handler([&](zx_status_t status) {
    EXPECT_EQ(status, ZX_ERR_ALREADY_BOUND);
    error_received = true;
  });
  device->ConnectToStream(0, stream2.NewRequest());
  RunLoopUntilFailureOr(error_received);

  // Attempt to bind second stream via rebind.
  SetFailOnError(stream2, "Stream");
  stream->Rebind(stream2.NewRequest());
  Sync(stream2);
}

TEST_F(DeviceTest, OrphanStream) {
  // Connect to the device.
  fuchsia::camera3::DevicePtr device;
  SetFailOnError(device, "Device");
  device_->GetHandler()(device.NewRequest());
  Sync(device);

  // Connect to the stream.
  fuchsia::camera3::StreamPtr stream;
  SetFailOnError(stream, "Stream");
  device->ConnectToStream(0, stream.NewRequest());
  Sync(stream);

  // Disconnect from the device.
  device = nullptr;

  // Reset the error handler to expect peer-closed.
  bool stream_error_received = false;
  stream.set_error_handler([&](zx_status_t status) {
    EXPECT_EQ(status, ZX_ERR_PEER_CLOSED);
    stream_error_received = true;
  });

  // Connect to the device as a new client. There is no way for a client to know when an existing
  // exclusive client disconnects, so it must retry periodically.
  fuchsia::camera3::DevicePtr device2;
  bool device_acquired = false;
  while (!device_acquired) {
    bool error_received = false;
    device2.set_error_handler([&](zx_status_t status) {
      EXPECT_EQ(status, ZX_ERR_ALREADY_BOUND);
      error_received = true;
    });
    device_->GetHandler()(device2.NewRequest());
    device2->GetIdentifier([&](fidl::StringPtr identifier) { device_acquired = true; });
    while (!HasFailure() && !device_acquired && !error_received) {
      RunLoopUntilIdle();
    }
    if (!device_acquired) {
      // Brief timeout to reduce logspam.
      zx::nanosleep(zx::deadline_after(zx::msec(100)));
    }
  }
  SetFailOnError(device2, "Device2");

  // Make sure the first stream is closed when the new device connects.
  RunLoopUntilFailureOr(stream_error_received);

  // The second client should be able to connect to the stream now.
  fuchsia::camera3::StreamPtr stream2;
  SetFailOnError(stream, "Stream2");
  device2->ConnectToStream(0, stream2.NewRequest());
  Sync(stream2);
}

}  // namespace camera
