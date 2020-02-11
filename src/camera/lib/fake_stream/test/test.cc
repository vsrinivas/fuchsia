// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/errors.h>
#include <zircon/syscalls/object.h>

#include "fuchsia/sysmem/cpp/fidl.h"
#include "src/camera/lib/fake_stream/fake_stream.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

static fuchsia::camera3::StreamProperties DefaultStreamProperties() {
  return {.image_format{.pixel_format{
                            .type = fuchsia::sysmem::PixelFormatType::NV12,
                        },
                        .coded_width = 256,
                        .coded_height = 128,
                        .bytes_per_row = 256,
                        .color_space{
                            .type = fuchsia::sysmem::ColorSpaceType::REC601_NTSC,
                        }},
          .frame_rate{
              .numerator = 30,
              .denominator = 1,
          },
          .supported_resolutions{{
              .coded_size{
                  .width = 256,
                  .height = 128,
              },
              .bytes_per_row = 256,
          }},
          .supports_crop_region = true};
}

class FakeStreamTest : public gtest::RealLoopFixture {
 protected:
  virtual void SetUp() override {}
  virtual void TearDown() override {}
};

TEST_F(FakeStreamTest, BadStreamProperties) {
  auto result = camera::FakeStream::Create({});
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(result.error(), ZX_ERR_INVALID_ARGS);
}

TEST_F(FakeStreamTest, CanConnectToStream) {
  auto result = camera::FakeStream::Create(DefaultStreamProperties());
  ASSERT_TRUE(result.is_ok());
  auto fake_stream = result.take_value();
  fuchsia::camera3::StreamPtr stream;
  stream.set_error_handler(
      [](zx_status_t status) { ADD_FAILURE() << "Stream server disconnected: " << status; });
  fake_stream->GetHandler()(stream.NewRequest());

  // Request a frame via the interface then add it with the fake.
  bool frame_received = false;
  fuchsia::camera3::FrameInfo info_received{};
  stream->GetNextFrame([&](fuchsia::camera3::FrameInfo info) {
    info_received = std::move(info);
    frame_received = true;
  });
  fuchsia::camera3::FrameInfo info_sent{
      .buffer_index = 42,
      .frame_counter = 1,
      .timestamp = 99,
  };
  zx::eventpair release_fence;
  ASSERT_EQ(zx::eventpair::create(0u, &release_fence, &info_sent.release_fence), ZX_OK);
  fuchsia::camera3::FrameInfo info_sent_clone{};
  ASSERT_EQ(info_sent.Clone(&info_sent_clone), ZX_OK);
  fake_stream->AddFrame(std::move(info_sent_clone));
  RunLoopUntil([&]() { return HasFailure() || frame_received; });

  ASSERT_TRUE(frame_received);
  EXPECT_EQ(info_sent.buffer_index, info_received.buffer_index);
  EXPECT_EQ(info_sent.frame_counter, info_received.frame_counter);
  EXPECT_EQ(info_sent.timestamp, info_received.timestamp);
  zx_info_handle_basic sent_fence_info{};
  ASSERT_EQ(info_sent.release_fence.get_info(ZX_INFO_HANDLE_BASIC, &sent_fence_info,
                                             sizeof(sent_fence_info), nullptr, nullptr),
            ZX_OK);
  zx_info_handle_basic received_fence_info{};
  ASSERT_EQ(info_sent.release_fence.get_info(ZX_INFO_HANDLE_BASIC, &received_fence_info,
                                             sizeof(received_fence_info), nullptr, nullptr),
            ZX_OK);
  EXPECT_EQ(sent_fence_info.koid, received_fence_info.koid);
}
