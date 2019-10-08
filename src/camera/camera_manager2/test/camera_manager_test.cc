// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/fzl/fdio.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>

#include <initializer_list>
#include <memory>
#include <vector>

#include <src/lib/fxl/logging.h>

#include "gtest/gtest.h"

namespace {

static constexpr uint32_t kBufferCountForCamping = 5;
static constexpr uint32_t kFakeImageMinWidth = 640;
static constexpr uint32_t kFakeImageMaxWidth = 2048;
static constexpr uint32_t kFakeImageMinHeight = 480;
static constexpr uint32_t kFakeImageMaxHeight = 1280;
static constexpr uint32_t kFakeImageMinBytesPerRow = 480;
static constexpr uint32_t kFakeImageMaxBytesPerRow = 0xfffffff;
static constexpr uint32_t kFakeImageBytesPerRowDivisor = 128;
static constexpr uint32_t kNumberOfLayers = 1;

fuchsia::sysmem::BufferCollectionConstraints GetFakeConstraints() {
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count_for_camping = kBufferCountForCamping;
  constraints.image_format_constraints_count = 1;
  auto& image_constraints = constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::NV12;
  image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::NV12;
  image_constraints.min_coded_width = kFakeImageMinWidth;
  image_constraints.max_coded_width = kFakeImageMaxWidth;
  image_constraints.min_coded_height = kFakeImageMinHeight;
  image_constraints.max_coded_height = kFakeImageMaxHeight;
  image_constraints.min_bytes_per_row = kFakeImageMinBytesPerRow;
  image_constraints.max_bytes_per_row = kFakeImageMaxBytesPerRow;
  image_constraints.layers = kNumberOfLayers;
  image_constraints.bytes_per_row_divisor = kFakeImageBytesPerRowDivisor;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::REC601_PAL;
  constraints.usage.video = fuchsia::sysmem::videoUsageHwEncoder;
  return constraints;
}

class CameraManagerTest : public gtest::RealLoopFixture {
 public:
  CameraManagerTest() : context_(sys::ComponentContext::Create()) {}

  // testing::Test implementation.
  void SetUp() override {
    ASSERT_EQ(ZX_OK, context_->svc()->Connect(sysmem_allocator_.NewRequest()));
    ASSERT_EQ(ZX_OK, context_->svc()->Connect(camera_manager_.NewRequest()));
  }
  void TearDown() override {}

  void GetToken(fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>* out_token) {
    // Create client_token which we'll hold on to to get our buffer_collection.
    fuchsia::sysmem::BufferCollectionTokenSyncPtr client_token;
    // Create camera_token to send to the camera manager
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> camera_token;
    // Start the allocation process
    ASSERT_EQ(ZX_OK, sysmem_allocator_->AllocateSharedCollection(client_token.NewRequest()));
    // Duplicate the token:
    client_token->Duplicate(std::numeric_limits<uint32_t>::max(), camera_token.NewRequest());
    ASSERT_EQ(ZX_OK, client_token->Sync());
    // Now convert our side into a Logical BufferCollection:
    sysmem_allocator_->BindSharedCollection(client_token.Unbind(),
                                            buffer_collection_.NewRequest());
    ASSERT_EQ(ZX_OK, buffer_collection_->SetConstraints(true, GetFakeConstraints()));
    *out_token = std::move(camera_token);
  }

 protected:
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection_;
  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::camera2::ManagerSyncPtr camera_manager_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
};

TEST_F(CameraManagerTest, CanConnectToStream) {
  EXPECT_EQ(ZX_OK, camera_manager_->AcknowledgeDeviceEvent());

// TODO(37729) Disabling flaky test while it is debugged.
#if 0

  fuchsia::camera2::StreamConstraints constraints;
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;
  fuchsia::camera2::StreamPtr stream;
  fuchsia::sysmem::ImageFormat_2 format;
  GetToken(&token);
  EXPECT_EQ(ZX_OK, camera_manager_->ConnectToStream(0, std::move(constraints), std::move(token),
                                                    stream.NewRequest(), &format));

  int frame_counter = 0;
  bool passed = false;
  stream.events().OnFrameAvailable = [&stream, &passed,
                                      &frame_counter](fuchsia::camera2::FrameAvailableInfo frame) {
    printf("Received FrameNotify Event %d at index: %u\n", frame_counter, frame.buffer_id);

    if (frame.frame_status == fuchsia::camera2::FrameStatus::OK) {
      stream->ReleaseFrame(frame.buffer_id);
      if (frame_counter++ > 10) {
        FXL_LOG(INFO) << "Counted 10 frames, stopping stream and quitting loop";
        stream->Stop();
        passed = true;
      }
    } else {
      FXL_LOG(ERROR) << "Error set on incoming frame. Error: "
                     << static_cast<int>(frame.frame_status);
    }
  };
  stream->Start();
  RunLoopWithTimeoutOrUntil([&passed]() { return passed; }, zx::sec(5));
  EXPECT_TRUE(passed);

#endif
}

}  // namespace
