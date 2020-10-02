// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/errors.h>

#include <limits>

#include "src/camera/bin/device/device_impl.h"
#include "src/camera/bin/device/stream_impl.h"
#include "src/camera/bin/device/test/fake_device_listener_registry.h"
#include "src/camera/bin/device/util.h"
#include "src/camera/lib/fake_controller/fake_controller.h"
#include "src/camera/lib/fake_legacy_stream/fake_legacy_stream.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace camera {

// No-op function.
static void nop() {}

static void check_stream_valid(zx_koid_t koid, fit::function<void(bool)> callback) {
  callback(true);
}

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
  DeviceTest()
      : context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()),
        fake_listener_registry_(async_get_default_dispatcher()) {
    fake_properties_.set_image_format({});
    fake_properties_.set_frame_rate({});
    fake_properties_.set_supports_crop_region({});
  }

  void SetUp() override {
    context_->svc()->Connect(allocator_.NewRequest());
    allocator_.set_error_handler(MakeErrorHandler("Sysmem Allocator"));

    fuchsia::camera2::hal::ControllerHandle controller;
    auto controller_result = FakeController::Create(controller.NewRequest());
    ASSERT_TRUE(controller_result.is_ok());
    controller_ = controller_result.take_value();

    fuchsia::sysmem::AllocatorHandle allocator;
    context_->svc()->Connect(allocator.NewRequest());

    fuchsia::ui::policy::DeviceListenerRegistryHandle registry;
    fake_listener_registry_.GetHandler()(registry.NewRequest());

    auto device_result =
        DeviceImpl::Create(std::move(controller), std::move(allocator), std::move(registry));
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
  fuchsia::camera3::StreamProperties2 fake_properties_;
  fuchsia::camera2::hal::StreamConfig fake_legacy_config_;
  FakeDeviceListenerRegistry fake_listener_registry_;
};

TEST_F(DeviceTest, CreateStreamNullConnection) {
  StreamImpl stream(fake_properties_, fake_legacy_config_, nullptr, check_stream_valid,
                    nop_stream_requested, nop);
}

TEST_F(DeviceTest, CreateStreamFakeLegacyStream) {
  fidl::InterfaceHandle<fuchsia::camera3::Stream> stream;
  StreamImpl stream_impl(
      fake_properties_, fake_legacy_config_, stream.NewRequest(), check_stream_valid,
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
  EXPECT_EQ(a.stream_configs.size(), b.streams().size());
}

TEST_F(DeviceTest, GetFrames) {
  fuchsia::camera3::StreamPtr stream;
  stream.set_error_handler(MakeErrorHandler("Stream"));
  constexpr uint32_t kBufferId1 = 42;
  constexpr uint32_t kBufferId2 = 17;
  constexpr uint32_t kMaxCampingBuffers = 1;
  std::unique_ptr<FakeLegacyStream> legacy_stream_fake;
  bool legacy_stream_created = false;
  auto stream_impl = std::make_unique<StreamImpl>(
      fake_properties_, fake_legacy_config_, stream.NewRequest(), check_stream_valid,
      [&](fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
          fidl::InterfaceRequest<fuchsia::camera2::Stream> request,
          fit::function<void(uint32_t)> callback, uint32_t format_index) {
        auto result = FakeLegacyStream::Create(std::move(request), dispatcher());
        ASSERT_TRUE(result.is_ok());
        legacy_stream_fake = result.take_value();
        token.BindSync()->Close();
        legacy_stream_created = true;
        callback(kMaxCampingBuffers);
      },
      nop);

  fuchsia::sysmem::BufferCollectionTokenPtr token;
  allocator_->AllocateSharedCollection(token.NewRequest());
  token->Sync([&] { stream->SetBufferCollection(std::move(token)); });
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
  constexpr fuchsia::sysmem::BufferCollectionConstraints constraints{
      .usage{.cpu = fuchsia::sysmem::cpuUsageRead},
      .min_buffer_count_for_camping = kMaxCampingBuffers,
      .image_format_constraints_count = 1,
      .image_format_constraints{
          {{.pixel_format{.type = fuchsia::sysmem::PixelFormatType::NV12},
            .color_spaces_count = 1,
            .color_space{{{.type = fuchsia::sysmem::ColorSpaceType::REC601_NTSC}}},
            .min_coded_width = 1,
            .min_coded_height = 1}}}};
  collection->SetConstraints(true, constraints);
  bool buffers_allocated_returned = false;
  collection->WaitForBuffersAllocated(
      [&](zx_status_t status, fuchsia::sysmem::BufferCollectionInfo_2 buffers) {
        EXPECT_EQ(status, ZX_OK);
        buffers_allocated_returned = true;
      });
  RunLoopUntil([&]() { return HasFailure() || buffers_allocated_returned; });
  ASSERT_FALSE(HasFailure());

  RunLoopUntil([&] { return HasFailure() || legacy_stream_fake->IsStreaming(); });
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

  // Make sure the stream recycles frames once its camping allocation is exhausted.
  // Also emulate a stuck client that does not return any frames.
  std::set<zx::eventpair> fences;
  uint32_t last_received_frame = -1;
  fit::function<void(fuchsia::camera3::FrameInfo)> on_next_frame;
  on_next_frame = [&](fuchsia::camera3::FrameInfo info) {
    last_received_frame = info.buffer_index;
    fences.insert(std::move(info.release_fence));
    stream->GetNextFrame(on_next_frame.share());
  };
  stream->GetNextFrame(on_next_frame.share());
  constexpr uint32_t kNumFrames = 17;
  for (uint32_t i = 0; i < kNumFrames; ++i) {
    fuchsia::camera2::FrameAvailableInfo frame_info{.buffer_id = i};
    frame_info.metadata.set_timestamp(0);
    ASSERT_EQ(legacy_stream_fake->SendFrameAvailable(std::move(frame_info)), ZX_OK);
    if (i < constraints.min_buffer_count_for_camping) {
      // Up to the camping limit, wait until the frames are received.
      RunLoopUntil([&] { return HasFailure() || last_received_frame == i; });
    } else {
      // After the camping limit is reached due to the emulated stuck client, verify that the Stream
      // recycles the oldest buffers first.
      RunLoopUntil([&] { return HasFailure() || !legacy_stream_fake->IsOutstanding(i); });
    }
  }
  fences.clear();

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
      fake_properties_, fake_legacy_config_, stream.NewRequest(), check_stream_valid,
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
  fuchsia::sysmem::BufferCollectionPtr buffers;
  SetFailOnError(buffers, "BufferCollection");
  bool buffers_allocated_returned = false;
  zx::vmo vmo;
  stream->WatchBufferCollection(
      [&](fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
        allocator_->BindSharedCollection(std::move(token), buffers.NewRequest());
        buffers->SetConstraints(true, {.usage{.cpu = fuchsia::sysmem::cpuUsageRead},
                                       .min_buffer_count_for_camping = 1});
        buffers->WaitForBuffersAllocated(
            [&](zx_status_t status, fuchsia::sysmem::BufferCollectionInfo_2 buffers) {
              EXPECT_EQ(status, ZX_OK);
              vmo = std::move(buffers.buffers[0].vmo);
              buffers_allocated_returned = true;
            });
      });
  RunLoopUntilFailureOr(buffers_allocated_returned);

  std::string vmo_name;
  RunLoopUntil([&] {
    vmo_name = fsl::GetObjectName(vmo.get());
    return vmo_name != "Sysmem-core";
  });
  EXPECT_EQ(vmo_name, "camera_c0_s0:0");

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
  buffers->Close();
  RunLoopUntilIdle();
}

// TODO(fxbug.dev/58063): Restore camera default exclusivity policy.
TEST_F(DeviceTest, DISABLED_DeviceClientDisconnect) {
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
    token->Sync([&] { stream2->SetBufferCollection(std::move(token)); });
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
  constexpr fuchsia::math::Size kRequestedSize3{.width = 1280, .height = 720};
  constexpr fuchsia::math::Size kExpectedSize3{.width = 1280, .height = 720};
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
  callback_received = false;
  stream->SetResolution(kRequestedSize3);
  stream->WatchResolution([&](fuchsia::math::Size coded_size) {
    EXPECT_GE(coded_size.width, kExpectedSize3.width);
    EXPECT_GE(coded_size.height, kExpectedSize3.height);
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

TEST_F(DeviceTest, SetCropRegion) {
  fuchsia::camera3::DevicePtr device;
  SetFailOnError(device, "Device");
  device_->GetHandler()(device.NewRequest());
  fuchsia::camera3::StreamPtr stream;
  device->ConnectToStream(0, stream.NewRequest());
  SetFailOnError(stream, "Stream");
  bool callback_received = false;
  stream->WatchCropRegion([&](std::unique_ptr<fuchsia::math::RectF> region) {
    EXPECT_EQ(region, nullptr);
    callback_received = true;
  });
  RunLoopUntilFailureOr(callback_received);
  constexpr fuchsia::math::RectF kCropRegion{.x = 0.1f, .y = 0.4f, .width = 0.7f, .height = 0.2f};
  callback_received = false;
  stream->WatchCropRegion([&](std::unique_ptr<fuchsia::math::RectF> region) {
    ASSERT_NE(region, nullptr);
    EXPECT_EQ(region->x, kCropRegion.x);
    EXPECT_EQ(region->y, kCropRegion.y);
    EXPECT_EQ(region->width, kCropRegion.width);
    EXPECT_EQ(region->height, kCropRegion.height);
    callback_received = true;
  });
  stream->SetCropRegion(std::make_unique<fuchsia::math::RectF>(kCropRegion));
  RunLoopUntilFailureOr(callback_received);
  bool error_received = false;
  stream.set_error_handler([&](zx_status_t status) {
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    error_received = true;
  });
  constexpr fuchsia::math::RectF kInvalidCropRegion{
      .x = 0.1f, .y = 0.4f, .width = 0.7f, .height = 0.7f};
  stream->SetCropRegion(std::make_unique<fuchsia::math::RectF>(kInvalidCropRegion));
  RunLoopUntilFailureOr(error_received);
}

TEST_F(DeviceTest, SoftwareMuteState) {
  // Connect to the device.
  fuchsia::camera3::DevicePtr device;
  SetFailOnError(device, "Device");
  device_->GetHandler()(device.NewRequest());
  bool watch_returned = false;
  device->WatchMuteState([&](bool software_muted, bool hardware_muted) {
    EXPECT_FALSE(software_muted);
    EXPECT_FALSE(hardware_muted);
    watch_returned = true;
  });
  RunLoopUntilFailureOr(watch_returned);

  // Connect to the stream.
  fuchsia::camera3::StreamPtr stream;
  SetFailOnError(stream, "Stream");
  device->ConnectToStream(0, stream.NewRequest());

  fuchsia::sysmem::BufferCollectionTokenPtr token;
  allocator_->AllocateSharedCollection(token.NewRequest());
  token->Sync([&] { stream->SetBufferCollection(std::move(token)); });
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> received_token;
  watch_returned = false;
  stream->WatchBufferCollection(
      [&](fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
        received_token = std::move(token);
        watch_returned = true;
      });
  RunLoopUntilFailureOr(watch_returned);

  fuchsia::sysmem::BufferCollectionPtr collection;
  collection.set_error_handler(MakeErrorHandler("Buffer Collection"));
  allocator_->BindSharedCollection(std::move(received_token), collection.NewRequest());
  collection->SetConstraints(
      true, {.usage{.cpu = fuchsia::sysmem::cpuUsageRead},
             .min_buffer_count_for_camping = 5,
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

  uint32_t next_buffer_id = 0;
  fit::closure send_frame = [&] {
    fuchsia::camera2::FrameAvailableInfo frame_info{
        .frame_status = fuchsia::camera2::FrameStatus::OK, .buffer_id = next_buffer_id};
    frame_info.metadata.set_timestamp(0);
    zx_status_t status = controller_->SendFrameViaLegacyStream(std::move(frame_info));
    if (status == ZX_ERR_SHOULD_WAIT || status == ZX_ERR_BAD_STATE) {
      // Keep trying until the device starts streaming.
      async::PostTask(async_get_default_dispatcher(), send_frame.share());
    } else {
      ++next_buffer_id;
      ASSERT_EQ(status, ZX_OK);
    }
  };

  // Because the device and stream protocols are asynchronous, mute requests may be handled by
  // streams while in a number of different states. Without deep hooks into the implementation, it
  // is impossible to force the stream into a particular state. Instead, this test repeatedly
  // toggles mute state in an attempt to exercise all cases.
  constexpr uint32_t kToggleCount = 50;
  for (uint32_t i = 0; i < kToggleCount; ++i) {
    // Get a frame (unmuted).
    bool frame_received = false;
    stream->GetNextFrame([&](fuchsia::camera3::FrameInfo info) { frame_received = true; });
    send_frame();
    RunLoopUntilFailureOr(frame_received);

    // Get a frame then immediately try to mute the device.
    bool mute_completed = false;
    bool muted_frame_requested = false;
    bool unmute_requested = false;
    bool unmuted_frame_received = false;
    fuchsia::camera3::Stream::GetNextFrameCallback callback =
        [&](fuchsia::camera3::FrameInfo info) {
          if (muted_frame_requested) {
            ASSERT_TRUE(unmute_requested)
                << "Frame requested after receiving mute callback returned anyway.";
          }
          if (unmute_requested) {
            unmuted_frame_received = true;
          } else {
            if (mute_completed) {
              muted_frame_requested = true;
            }
            stream->GetNextFrame(callback.share());
            send_frame();
          }
        };
    callback({});
    uint32_t mute_buffer_id_begin = next_buffer_id;
    uint32_t mute_buffer_id_end = mute_buffer_id_begin;
    device->SetSoftwareMuteState(true, [&] {
      mute_completed = true;
      mute_buffer_id_end = next_buffer_id;
    });
    RunLoopUntilFailureOr(mute_completed);

    // Make sure all buffers were returned.
    for (uint32_t j = mute_buffer_id_begin; j < mute_buffer_id_end; ++j) {
      RunLoopUntil(
          [&] { return HasFailure() || !controller_->LegacyStreamBufferIsOutstanding(j); });
    }

    // Unmute the device to get the last frame. Note that frames received while internally muted are
    // discarded, so repeated sending of frames is necessary.
    unmute_requested = true;
    device->SetSoftwareMuteState(false, [] {});
    while (!HasFailure() && !unmuted_frame_received) {
      send_frame();
      // Delay each attempt to avoid flooding the channel.
      RunLoopWithTimeout(zx::msec(10));
    }

    ASSERT_FALSE(HasFailure());
  }

  collection->Close();
}

TEST_F(DeviceTest, HardwareMuteState) {
  // Connect to the device.
  fuchsia::camera3::DevicePtr device;
  SetFailOnError(device, "Device");
  device_->GetHandler()(device.NewRequest());

  // Device should start unmuted.
  bool watch_returned = false;
  device->WatchMuteState([&](bool software_muted, bool hardware_muted) {
    EXPECT_FALSE(software_muted);
    EXPECT_FALSE(hardware_muted);
    watch_returned = true;
  });
  RunLoopUntilFailureOr(watch_returned);

  // Verify mute event.
  watch_returned = false;
  device->WatchMuteState([&](bool software_muted, bool hardware_muted) {
    EXPECT_FALSE(software_muted);
    EXPECT_TRUE(hardware_muted);
    watch_returned = true;
  });
  fuchsia::ui::input::MediaButtonsEvent mute_event;
  mute_event.set_mic_mute(true);
  fake_listener_registry_.SendMediaButtonsEvent(std::move(mute_event));
  RunLoopUntilFailureOr(watch_returned);

  // Verify unmute event.
  watch_returned = false;
  device->WatchMuteState([&](bool software_muted, bool hardware_muted) {
    EXPECT_FALSE(software_muted);
    EXPECT_FALSE(hardware_muted);
    watch_returned = true;
  });
  fuchsia::ui::input::MediaButtonsEvent unmute_event;
  unmute_event.set_mic_mute(false);
  fake_listener_registry_.SendMediaButtonsEvent(std::move(unmute_event));
  RunLoopUntilFailureOr(watch_returned);
}

TEST_F(DeviceTest, GetProperties) {
  fuchsia::camera3::DevicePtr device;
  SetFailOnError(device, "Device");
  device_->GetHandler()(device.NewRequest());
  bool configs_returned = false;
  std::vector<fuchsia::camera3::Configuration> configs;
  device->GetConfigurations([&](std::vector<fuchsia::camera3::Configuration> configurations) {
    configs = std::move(configurations);
    configs_returned = true;
  });
  RunLoopUntilFailureOr(configs_returned);
  fuchsia::camera3::StreamPtr stream;
  device->ConnectToStream(0, stream.NewRequest());
  bool properties_returned = false;
  stream->GetProperties([&](fuchsia::camera3::StreamProperties properties) {
    EXPECT_EQ(properties.supports_crop_region, configs[0].streams[0].supports_crop_region);
    EXPECT_EQ(properties.frame_rate.numerator, configs[0].streams[0].frame_rate.numerator);
    EXPECT_EQ(properties.frame_rate.denominator, configs[0].streams[0].frame_rate.denominator);
    EXPECT_EQ(properties.image_format.coded_width, configs[0].streams[0].image_format.coded_width);
    EXPECT_EQ(properties.image_format.coded_height,
              configs[0].streams[0].image_format.coded_height);
    properties_returned = true;
  });
  RunLoopUntilFailureOr(properties_returned);
  properties_returned = false;
  stream->GetProperties2([&](fuchsia::camera3::StreamProperties2 properties) {
    ASSERT_FALSE(properties.supported_resolutions().empty());
    EXPECT_EQ(static_cast<uint32_t>(properties.supported_resolutions().at(0).width),
              configs[0].streams[0].image_format.coded_width);
    EXPECT_EQ(static_cast<uint32_t>(properties.supported_resolutions().at(0).height),
              configs[0].streams[0].image_format.coded_height);
    properties_returned = true;
  });
  RunLoopUntilFailureOr(properties_returned);
}

TEST_F(DeviceTest, BindFailureOk) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("BindFailureOk");
  volatile bool started = false;
  volatile bool quit = false;
  volatile bool done = false;
  async::PostTask(loop.dispatcher(), [&] {
    started = true;
    while (!quit) {
      zx::nanosleep({});
    }
    fuchsia::sysmem::AllocatorPtr allocator;
    allocator.NewRequest().TakeChannel().reset();
    EXPECT_FALSE(WaitForFreeSpace(allocator, {}));
    done = true;
  });
  while (!started) {
    zx::nanosleep({});
  }
  loop.Quit();
  quit = true;
  while (!done) {
    zx::nanosleep({});
  }
  loop.Shutdown();
}

TEST_F(DeviceTest, DISABLED_SetBufferCollectionAgainWhileFramesHeld) {
  constexpr uint32_t kCycleCount = 10;
  uint32_t cycle = 0;

  fuchsia::camera3::StreamPtr stream;
  constexpr uint32_t kMaxCampingBuffers = 1;
  std::array<std::unique_ptr<FakeLegacyStream>, kCycleCount> legacy_stream_fakes;
  auto stream_impl = std::make_unique<StreamImpl>(
      fake_properties_, fake_legacy_config_, stream.NewRequest(), check_stream_valid,
      [&](fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
          fidl::InterfaceRequest<fuchsia::camera2::Stream> request,
          fit::function<void(uint32_t)> callback, uint32_t format_index) {
        auto result = FakeLegacyStream::Create(std::move(request), dispatcher());
        ASSERT_TRUE(result.is_ok());
        legacy_stream_fakes[cycle] = result.take_value();
        token.BindSync()->Close();
        callback(kMaxCampingBuffers);
      },
      nop);

  std::vector<fuchsia::camera3::FrameInfo> frames(kCycleCount);
  for (cycle = 0; cycle < kCycleCount; ++cycle) {
    fuchsia::sysmem::BufferCollectionTokenPtr token;
    allocator_->AllocateSharedCollection(token.NewRequest());
    token->Sync([&] { stream->SetBufferCollection(std::move(token)); });
    stream->SetBufferCollection(std::move(token));
    bool frame_received = false;
    stream->WatchBufferCollection([&](fuchsia::sysmem::BufferCollectionTokenHandle token) {
      fuchsia::sysmem::BufferCollectionSyncPtr collection;
      allocator_->BindSharedCollection(std::move(token), collection.NewRequest());
      constexpr fuchsia::sysmem::BufferCollectionConstraints constraints{
          .usage{.cpu = fuchsia::sysmem::cpuUsageRead},
          .min_buffer_count_for_camping = kMaxCampingBuffers,
          .image_format_constraints_count = 1,
          .image_format_constraints{
              {{.pixel_format{.type = fuchsia::sysmem::PixelFormatType::NV12},
                .color_spaces_count = 1,
                .color_space{{{.type = fuchsia::sysmem::ColorSpaceType::REC601_NTSC}}},
                .min_coded_width = 1,
                .min_coded_height = 1}}}};
      collection->SetConstraints(true, constraints);
      zx_status_t status = ZX_OK;
      fuchsia::sysmem::BufferCollectionInfo_2 buffers;
      collection->WaitForBuffersAllocated(&status, &buffers);
      EXPECT_EQ(status, ZX_OK);
      collection->Close();
      stream->GetNextFrame([&](fuchsia::camera3::FrameInfo info) {
        // Keep the frame; do not release it.
        frames[cycle] = std::move(info);
        frame_received = true;
      });
      fuchsia::camera2::FrameAvailableInfo frame_info;
      frame_info.frame_status = fuchsia::camera2::FrameStatus::OK;
      frame_info.buffer_id = cycle;
      frame_info.metadata.set_timestamp(0);
      while (!HasFailure() && !legacy_stream_fakes[cycle]->IsStreaming()) {
        RunLoopUntilIdle();
      }
      ASSERT_EQ(legacy_stream_fakes[cycle]->SendFrameAvailable(std::move(frame_info)), ZX_OK);
    });
    RunLoopUntilFailureOr(frame_received);
  }
}

TEST_F(DeviceTest, FrameWaiterTest) {
  {  // Test that destructor of a non-triggered waiter does not panic.
    zx::eventpair client;
    zx::eventpair server;
    ASSERT_EQ(zx::eventpair::create(0, &client, &server), ZX_OK);
    bool signaled = false;
    {
      FrameWaiter waiter(dispatcher(), std::move(server), [&] { signaled = true; });
      RunLoopUntilIdle();
    }
    RunLoopUntilIdle();
    EXPECT_FALSE(signaled);
  }

  {  // Test that closing the client endpoint triggers the wait.
    zx::eventpair client;
    zx::eventpair server;
    ASSERT_EQ(zx::eventpair::create(0, &client, &server), ZX_OK);
    bool signaled = false;
    FrameWaiter waiter(dispatcher(), std::move(server), [&] { signaled = true; });
    client.reset();
    RunLoopUntilFailureOr(signaled);
  }
}

TEST_F(DeviceTest, BadToken) {
  fuchsia::camera3::DevicePtr device;
  SetFailOnError(device, "Device");
  device_->GetHandler()(device.NewRequest());
  fuchsia::camera3::StreamPtr stream;
  bool stream_disconnected = false;
  stream.set_error_handler([&](zx_status_t status) {
    EXPECT_EQ(status, ZX_ERR_BAD_STATE);
    stream_disconnected = true;
  });
  device->ConnectToStream(0, stream.NewRequest());
  fuchsia::sysmem::BufferCollectionTokenPtr token;
  // Create and close the server endpoint of the token.
  token.NewRequest().TakeChannel().reset();
  stream->SetBufferCollection(std::move(token));
  stream->WatchBufferCollection([&](fuchsia::sysmem::BufferCollectionTokenHandle token) {
    ADD_FAILURE() << "Watch should not return when given an invalid token.";
  });
  RunLoopUntilFailureOr(stream_disconnected);
}

TEST_F(DeviceTest, StuckToken) {
  fuchsia::camera3::DevicePtr device;
  SetFailOnError(device, "Device");
  device_->GetHandler()(device.NewRequest());
  fuchsia::camera3::StreamPtr stream;
  bool stream_disconnected = false;
  stream.set_error_handler([&](zx_status_t status) {
    EXPECT_EQ(status, ZX_ERR_BAD_STATE);
    stream_disconnected = true;
  });
  device->ConnectToStream(0, stream.NewRequest());
  fuchsia::sysmem::BufferCollectionTokenPtr token;
  // Create the server endpoint of the token but do not close it or pass it to sysmem.
  auto request = token.NewRequest();
  stream->SetBufferCollection(std::move(token));
  stream->WatchBufferCollection([&](fuchsia::sysmem::BufferCollectionTokenHandle token) {
    ADD_FAILURE() << "Watch should not return when given a token not visible to sysmem.";
  });
  RunLoopUntilFailureOr(stream_disconnected);
}

TEST_F(DeviceTest, GoodTokenWithSync) {
  fuchsia::camera3::DevicePtr device;
  SetFailOnError(device, "Device");
  device_->GetHandler()(device.NewRequest());
  fuchsia::camera3::StreamPtr stream;
  SetFailOnError(stream, "Stream");
  device->ConnectToStream(0, stream.NewRequest());
  fuchsia::sysmem::BufferCollectionTokenPtr token;
  auto request = token.NewRequest();
  token->Sync([&] { stream->SetBufferCollection(std::move(token)); });
  bool watch_returned = false;
  stream->WatchBufferCollection([&](fuchsia::sysmem::BufferCollectionTokenHandle token) {
    token.BindSync()->Close();
    watch_returned = true;
  });
  auto kDelay = zx::msec(250);
  async::PostDelayedTask(
      dispatcher(), [&] { allocator_->AllocateSharedCollection(std::move(request)); }, kDelay);
  RunLoopUntilFailureOr(watch_returned);
}

}  // namespace camera
