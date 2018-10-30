// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/buffer.h"
#include "garnet/lib/ui/gfx/resources/material.h"
#include "garnet/lib/ui/gfx/resources/nodes/shape_node.h"
#include "garnet/lib/ui/gfx/resources/shapes/circle_shape.h"
#include "garnet/lib/ui/gfx/tests/session_test.h"
#include "garnet/lib/ui/gfx/tests/vk_session_test.h"
#include "public/lib/escher/test/gtest_vulkan.h"

#include "lib/ui/scenic/cpp/commands.h"

#include "gtest/gtest.h"

namespace scenic_impl {
namespace gfx {
namespace test {

TEST_F(SessionTest, ScheduleUpdateOutOfOrder) {
  EXPECT_TRUE(
      session_->ScheduleUpdate(1, std::vector<::fuchsia::ui::gfx::Command>(),
                               ::fidl::VectorPtr<zx::event>(),
                               ::fidl::VectorPtr<zx::event>(), [](auto) {}));
  EXPECT_FALSE(
      session_->ScheduleUpdate(0, std::vector<::fuchsia::ui::gfx::Command>(),
                               ::fidl::VectorPtr<zx::event>(),
                               ::fidl::VectorPtr<zx::event>(), [](auto) {}));
  ExpectLastReportedError(
      "scenic_impl::gfx::Session: Present called with out-of-order "
      "presentation "
      "time. requested presentation time=0, last scheduled presentation "
      "time=1.");
}

TEST_F(SessionTest, ScheduleUpdateInOrder) {
  EXPECT_TRUE(
      session_->ScheduleUpdate(1, std::vector<::fuchsia::ui::gfx::Command>(),
                               ::fidl::VectorPtr<zx::event>(),
                               ::fidl::VectorPtr<zx::event>(), [](auto) {}));
  EXPECT_TRUE(
      session_->ScheduleUpdate(1, std::vector<::fuchsia::ui::gfx::Command>(),
                               ::fidl::VectorPtr<zx::event>(),
                               ::fidl::VectorPtr<zx::event>(), [](auto) {}));
  ExpectLastReportedError(nullptr);
}

TEST_F(SessionTest, ResourceIdAlreadyUsed) {
  EXPECT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(1)));
  EXPECT_TRUE(Apply(scenic::NewCreateShapeNodeCmd(2)));
  ExpectLastReportedError(nullptr);
  EXPECT_FALSE(Apply(scenic::NewCreateShapeNodeCmd(2)));
  ExpectLastReportedError(
      "scenic::gfx::ResourceMap::AddResource(): resource with ID 2 already "
      "exists.");
}

TEST_F(SessionTest, AddAndRemoveResource) {
  EXPECT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(1)));
  EXPECT_TRUE(Apply(scenic::NewCreateShapeNodeCmd(2)));
  EXPECT_TRUE(Apply(scenic::NewCreateShapeNodeCmd(3)));
  EXPECT_TRUE(Apply(scenic::NewCreateShapeNodeCmd(4)));
  EXPECT_TRUE(Apply(scenic::NewAddChildCmd(1, 2)));
  EXPECT_TRUE(Apply(scenic::NewAddPartCmd(1, 3)));
  EXPECT_EQ(4U, session_->GetTotalResourceCount());
  EXPECT_EQ(4U, session_->GetMappedResourceCount());

  // Even though we release node 2 and 3, they continue to exist because they
  // referenced by node 1.  Only node 4 is destroyed.
  EXPECT_TRUE(Apply(scenic::NewReleaseResourceCmd(2)));
  EXPECT_TRUE(Apply(scenic::NewReleaseResourceCmd(3)));
  EXPECT_TRUE(Apply(scenic::NewReleaseResourceCmd(4)));
  EXPECT_EQ(3U, session_->GetTotalResourceCount());
  EXPECT_EQ(1U, session_->GetMappedResourceCount());

  // Releasing node 1 causes nodes 1-3 to be destroyed.
  EXPECT_TRUE(Apply(scenic::NewReleaseResourceCmd(1)));
  EXPECT_EQ(0U, session_->GetTotalResourceCount());
  EXPECT_EQ(0U, session_->GetMappedResourceCount());
}

TEST_F(SessionTest, Labeling) {
  const ResourceId kNodeId = 1;
  const std::string kShortLabel = "test!";
  const std::string kLongLabel =
      std::string(::fuchsia::ui::gfx::kLabelMaxLength, 'x');
  const std::string kTooLongLabel =
      std::string(::fuchsia::ui::gfx::kLabelMaxLength + 1, '?');

  EXPECT_TRUE(Apply(scenic::NewCreateShapeNodeCmd(kNodeId)));
  auto shape_node = FindResource<ShapeNode>(kNodeId);
  EXPECT_TRUE(shape_node->label().empty());
  EXPECT_TRUE(Apply(scenic::NewSetLabelCmd(kNodeId, kShortLabel)));
  EXPECT_EQ(kShortLabel, shape_node->label());
  EXPECT_TRUE(Apply(scenic::NewSetLabelCmd(kNodeId, kLongLabel)));
  EXPECT_EQ(kLongLabel, shape_node->label());
  EXPECT_TRUE(Apply(scenic::NewSetLabelCmd(kNodeId, kTooLongLabel)));
  EXPECT_EQ(kTooLongLabel.substr(0, ::fuchsia::ui::gfx::kLabelMaxLength),
            shape_node->label());
  EXPECT_TRUE(Apply(scenic::NewSetLabelCmd(kNodeId, "")));
  EXPECT_TRUE(shape_node->label().empty());

  // Bypass the truncation performed by session helpers.
  shape_node->SetLabel(kTooLongLabel);
  EXPECT_EQ(kTooLongLabel.substr(0, ::fuchsia::ui::gfx::kLabelMaxLength),
            shape_node->label());
}

using BufferSessionTest = VkSessionTest;

VK_TEST_F(BufferSessionTest, BufferAliasing) {
  size_t vmo_size = 1024;
  size_t offset = 512;

  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(vmo_size, 0u, &vmo);
  EXPECT_TRUE(status == ZX_OK);

  zx::vmo dup_vmo;
  status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_vmo);
  EXPECT_TRUE(status == ZX_OK);

  // TODO(SCN-273): When VK_HOST_MEMORY is supported, this test should be
  // converted over. It only works now because we test on UMA platforms.
  EXPECT_TRUE(Apply(scenic::NewCreateMemoryCmd(
      1, std::move(dup_vmo), vmo_size,
      fuchsia::images::MemoryType::VK_DEVICE_MEMORY)));
  EXPECT_TRUE(Apply(scenic::NewCreateBufferCmd(2, 1, 0, vmo_size)));
  EXPECT_TRUE(
      Apply(scenic::NewCreateBufferCmd(3, 1, offset, vmo_size - offset)));

  auto base_buffer = FindResource<Buffer>(2);
  auto offset_buffer = FindResource<Buffer>(3);

  EXPECT_TRUE(base_buffer);
  EXPECT_TRUE(base_buffer->escher_buffer());
  EXPECT_TRUE(base_buffer->escher_buffer()->host_ptr());

  EXPECT_TRUE(offset_buffer);
  EXPECT_TRUE(offset_buffer->escher_buffer());
  EXPECT_TRUE(offset_buffer->escher_buffer()->host_ptr());

  auto shared_vmo = fxl::AdoptRef(
      new fsl::SharedVmo(std::move(vmo), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE));
  uint8_t* raw_memory = static_cast<uint8_t*>(shared_vmo->Map());
  EXPECT_TRUE(raw_memory);

  memset(raw_memory, 0, vmo_size);
  raw_memory[512] = 1;
  EXPECT_EQ(base_buffer->escher_buffer()->host_ptr()[0], 0);
  EXPECT_EQ(base_buffer->escher_buffer()->host_ptr()[512], 1);
  EXPECT_EQ(offset_buffer->escher_buffer()->host_ptr()[0], 1);
}

// TODO:
// - test that FindResource() cannot return resources that have the wrong type.

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
