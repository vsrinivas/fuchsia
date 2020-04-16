// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ui/scenic/cpp/commands_sizing.h>

#include <gtest/gtest.h>

namespace measure_tape {
namespace fuchsia {
namespace ui {
namespace scenic {

TEST(CommandsSizingTest, SendPointerInputCmd) {
  ::fuchsia::ui::input::SendPointerInputCmd send_pointer_input_cmd;

  ::fuchsia::ui::input::Command input_cmd;
  input_cmd.set_send_pointer_input(send_pointer_input_cmd);

  ::fuchsia::ui::scenic::Command cmd;
  cmd.set_input(std::move(input_cmd));

  auto size = Measure(cmd);
  EXPECT_EQ(size.num_bytes, 104 /* 24 + 24 + 56 */);
  EXPECT_EQ(size.num_handles, 0);
}

TEST(CommandsSizingTest, CreateResourceCmdWithMemoryArgs) {
  ::fuchsia::ui::gfx::MemoryArgs memory_args; // 24 bytes

  ::fuchsia::ui::gfx::ResourceArgs resource_args; // 24 + 24 bytes
  resource_args.set_memory(std::move(memory_args));

  ::fuchsia::ui::gfx::CreateResourceCmd create_resource_cmd; // 8 + 24 + 24 bytes
  create_resource_cmd.resource = std::move(resource_args);

  ::fuchsia::ui::gfx::Command gfx_cmd; // 24 + 8 + 24 + 24 bytes
  gfx_cmd.set_create_resource(std::move(create_resource_cmd));

  ::fuchsia::ui::scenic::Command cmd; // 24 + 24 + 8 + 24 + 24 bytes
  cmd.set_gfx(std::move(gfx_cmd));

  auto size = Measure(cmd);
  EXPECT_EQ(size.num_bytes, 104 /* 24 + 24 + 8 + 24 + 24 */);
  EXPECT_EQ(size.num_handles, 1);
}

TEST(CommandsSizingTest, CreateResourceCmdWithImagePipeCompositorArgs) {
  ::fuchsia::ui::gfx::ImagePipeCompositorArgs image_pipe_compositor_args; // 8 bytes, and 1 handle

  ::fuchsia::ui::gfx::ResourceArgs resource_args; // 24 + 8 bytes
  resource_args.set_image_pipe_compositor(std::move(image_pipe_compositor_args));

  ::fuchsia::ui::gfx::CreateResourceCmd create_resource_cmd; // 8 + 24 + 8 bytes
  create_resource_cmd.resource = std::move(resource_args);

  ::fuchsia::ui::gfx::Command gfx_cmd; // 24 + 8 + 24 + 8 bytes
  gfx_cmd.set_create_resource(std::move(create_resource_cmd));

  ::fuchsia::ui::scenic::Command cmd; // 24 + 24 + 8 + 24 + 8 bytes
  cmd.set_gfx(std::move(gfx_cmd));

  auto size = Measure(cmd);
  EXPECT_EQ(size.num_bytes, 88 /* 24 + 24 + 8 + 24 + 8 */);
  EXPECT_EQ(size.num_handles, 1);
}

TEST(CommandsSizingTest, ViewsCmd) {
  // While ::fuchsia::ui::views::Command is deprecated, and should not be used, we
  // still want to ensure that measuring such commands is correct.

  ::fuchsia::ui::views::Command views_cmd; // 24 + 8 bytes
  views_cmd.set_empty(6);

  ::fuchsia::ui::scenic::Command cmd; // 24 + 24 + 8 bytes
  cmd.set_views(std::move(views_cmd));

  auto size = Measure(cmd);
  EXPECT_EQ(size.num_bytes, 56 /* 24 + 24 + 8 */);
  EXPECT_EQ(size.num_handles, 0);
}

TEST(CommandsSizingTest, CreateResourceCmdWithViewArgs3) {
  ::fuchsia::ui::gfx::ViewArgs3 view_args3; // 32 bytes, and 3 handdles

  ::fuchsia::ui::gfx::ResourceArgs resource_args; // 24 + 32 bytes
  resource_args.set_view3(std::move(view_args3));

  ::fuchsia::ui::gfx::CreateResourceCmd create_resource_cmd; // 8 + 24 + 32 bytes
  create_resource_cmd.resource = std::move(resource_args);

  ::fuchsia::ui::gfx::Command gfx_cmd; // 24 + 8 + 24 + 32 bytes
  gfx_cmd.set_create_resource(std::move(create_resource_cmd));

  ::fuchsia::ui::scenic::Command cmd; // 24 + 24 + 8 + 24 + 32 bytes
  cmd.set_gfx(std::move(gfx_cmd));

  auto size = Measure(cmd);
  EXPECT_EQ(size.num_bytes, 112 /* 24 + 24 + 8 + 24 + 32 */);
  EXPECT_EQ(size.num_handles, 3);
}

TEST(CommandsSizingTest, CreateResourceCmdWithViewArgs3WithDebugName) {
  ::fuchsia::ui::gfx::ViewArgs3 view_args3; // 32 bytes, and 3 handdles
  view_args3.debug_name = "Hello, World!"; // 13 characters, aligned, so 16 bytes

  ::fuchsia::ui::gfx::ResourceArgs resource_args; // 24 + 32 + 16 bytes
  resource_args.set_view3(std::move(view_args3));

  ::fuchsia::ui::gfx::CreateResourceCmd create_resource_cmd; // 8 + 24 + 32 + 16 bytes
  create_resource_cmd.resource = std::move(resource_args);

  ::fuchsia::ui::gfx::Command gfx_cmd; // 24 + 8 + 24 + 32 + 16 bytes
  gfx_cmd.set_create_resource(std::move(create_resource_cmd));

  ::fuchsia::ui::scenic::Command cmd; // 24 + 24 + 8 + 24 + 32 + 16 bytes
  cmd.set_gfx(std::move(gfx_cmd));

  auto size = Measure(cmd);
  EXPECT_EQ(size.num_bytes, 128 /* 24 + 24 + 8 + 24 + 32 + 16 */);
  EXPECT_EQ(size.num_handles, 3);
}

TEST(CommandsSizingTest, CreateResourceCmdWithSetStereoCameraProjectionCmd) {
  // SetStereoCameraProjectionCmd contains an array, which are not yet sized.
  // Instead, we max out to failsafe to flush early.
  ::fuchsia::ui::gfx::SetStereoCameraProjectionCmd stereo_camera_projection_cmd;

  ::fuchsia::ui::gfx::Command gfx_cmd; // maxed out
  gfx_cmd.set_set_stereo_camera_projection(std::move(stereo_camera_projection_cmd));

  ::fuchsia::ui::scenic::Command cmd; // maxed out
  cmd.set_gfx(std::move(gfx_cmd));

  auto size = Measure(cmd);
  EXPECT_EQ(size.num_bytes, ZX_CHANNEL_MAX_MSG_BYTES);
  EXPECT_EQ(size.num_handles, ZX_CHANNEL_MAX_MSG_HANDLES);
}

}  // scenic
}  // ui
}  // fuchsia
}  // measure_tape
