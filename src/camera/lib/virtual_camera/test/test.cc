// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/camera/lib/virtual_camera/virtual_camera.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

class VirtualCameraTest : public gtest::RealLoopFixture {
 public:
  VirtualCameraTest() { context_ = sys::ComponentContext::CreateAndServeOutgoingDirectory(); }

 protected:
  virtual void SetUp() override {
    context_->svc()->Connect(allocator_.NewRequest());
    SetFailOnError(allocator_);
    fidl::InterfaceHandle<fuchsia::sysmem::Allocator> allocator;
    context_->svc()->Connect(allocator.NewRequest());
    auto result = camera::VirtualCamera::Create(std::move(allocator));
    ASSERT_TRUE(result.is_ok());
    virtual_camera_ = result.take_value();
  }

  virtual void TearDown() override {
    virtual_camera_ = nullptr;
    allocator_ = nullptr;
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

  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::sysmem::AllocatorPtr allocator_;
  std::unique_ptr<camera::VirtualCamera> virtual_camera_;
};

TEST_F(VirtualCameraTest, FramesReceived) {
  fuchsia::camera3::DevicePtr camera;
  SetFailOnError(camera, "Camera");
  virtual_camera_->GetHandler()(camera.NewRequest());

  bool configurations_received = false;
  camera->GetConfigurations([&](std::vector<fuchsia::camera3::Configuration> configurations) {
    ASSERT_FALSE(configurations.empty());
    ASSERT_FALSE(configurations[0].streams.empty());
    configurations_received = true;
  });
  RunLoopUntilFailureOr(configurations_received);
  ASSERT_FALSE(HasFailure());

  fuchsia::camera3::StreamPtr stream;
  SetFailOnError(stream, "Stream");
  camera->ConnectToStream(0, stream.NewRequest());

  fuchsia::sysmem::BufferCollectionTokenPtr token;
  allocator_->AllocateSharedCollection(token.NewRequest());
  token->Sync([&] { stream->SetBufferCollection(std::move(token)); });
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> client_token;
  bool token_received = false;
  stream->WatchBufferCollection(
      [&](fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
        client_token = std::move(token);
        token_received = true;
      });
  RunLoopUntilFailureOr(token_received);

  fuchsia::sysmem::BufferCollectionPtr collection;
  SetFailOnError(collection);
  allocator_->BindSharedCollection(std::move(client_token), collection.NewRequest());

  fuchsia::sysmem::BufferCollectionConstraints constraints{
      .usage{.cpu = fuchsia::sysmem::cpuUsageRead}, .min_buffer_count_for_camping = 2};
  collection->SetConstraints(true, constraints);
  fuchsia::sysmem::BufferCollectionInfo_2 buffers_received;
  bool buffers_allocated = false;
  collection->WaitForBuffersAllocated(
      [&](zx_status_t status, fuchsia::sysmem::BufferCollectionInfo_2 buffers) {
        ASSERT_EQ(status, ZX_OK);
        buffers_received = std::move(buffers);
        buffers_allocated = true;
      });
  RunLoopUntilFailureOr(buffers_allocated);
  ASSERT_FALSE(HasFailure());
  collection->Close();

  constexpr uint32_t kTargetFrameCount = 11;
  uint32_t frames_received = 0;
  bool all_frames_received = false;
  fit::function<void(fuchsia::camera3::FrameInfo)> check_frame;
  check_frame = [&](fuchsia::camera3::FrameInfo info) {
    fzl::VmoMapper mapper;
    ASSERT_EQ(mapper.Map(buffers_received.buffers[info.buffer_index].vmo, 0,
                         buffers_received.settings.buffer_settings.size_bytes, ZX_VM_PERM_READ),
              ZX_OK);
    auto result = virtual_camera_->CheckFrame(mapper.start(), mapper.size(), info);
    EXPECT_TRUE(result.is_ok()) << result.error();
    result = virtual_camera_->CheckFrame(mapper.start(), 0, info);
    EXPECT_TRUE(result.is_error());
    mapper.Unmap();
    std::vector<char> zeros(0, mapper.size());
    result = virtual_camera_->CheckFrame(zeros.data(), zeros.size(), info);
    EXPECT_TRUE(result.is_error());
    if (++frames_received < kTargetFrameCount) {
      stream->GetNextFrame(check_frame.share());
    } else {
      all_frames_received = true;
    }
  };
  stream->GetNextFrame(check_frame.share());
  RunLoopUntilFailureOr(all_frames_received);
  ASSERT_FALSE(HasFailure());
}
