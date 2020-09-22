// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/ui/scenic/cpp/commands.h>

#include <gtest/gtest.h>

#include "src/ui/lib/escher/test/common/gtest_vulkan.h"
#include "src/ui/scenic/lib/gfx/engine/scene_graph.h"
#include "src/ui/scenic/lib/gfx/resources/buffer.h"
#include "src/ui/scenic/lib/gfx/resources/material.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/shape_node.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/circle_shape.h"
#include "src/ui/scenic/lib/gfx/tests/session_test.h"
#include "src/ui/scenic/lib/gfx/tests/vk_session_test.h"
#include "src/ui/scenic/lib/gfx/tests/vk_util.h"

using scheduling::Present2Info;

namespace scenic_impl {
namespace gfx {
namespace test {

TEST_F(SessionTest, AddPart_ShouldReturnFalse) {
  fuchsia::ui::gfx::AddPartCmd add_part_command;
  add_part_command.node_id = 0;
  add_part_command.part_id = 1;
  fuchsia::ui::gfx::Command command;
  command.set_add_part(std::move(add_part_command));

  EXPECT_FALSE(Apply(std::move(command)));
}

TEST_F(SessionTest, TakeSnapshot_ShouldReturnFalse) {
  fuchsia::ui::gfx::TakeSnapshotCmdDEPRECATED snapshot_command;
  snapshot_command.node_id = 0;
  fuchsia::ui::gfx::Command command;
  command.set_take_snapshot_cmd(std::move(snapshot_command));
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
  EXPECT_EQ(4U, session()->GetTotalResourceCount());
  EXPECT_EQ(4U, session()->GetMappedResourceCount());

  // Even though we release node 2 and 3, they continue to exist because they
  // referenced by node 1.  Only node 4 is destroyed.
  EXPECT_TRUE(Apply(scenic::NewReleaseResourceCmd(2)));
  EXPECT_TRUE(Apply(scenic::NewReleaseResourceCmd(3)));
  EXPECT_TRUE(Apply(scenic::NewReleaseResourceCmd(4)));
  EXPECT_EQ(2U, session()->GetTotalResourceCount());
  EXPECT_EQ(1U, session()->GetMappedResourceCount());

  // Releasing node 1 causes nodes 1-3 to be destroyed.
  EXPECT_TRUE(Apply(scenic::NewReleaseResourceCmd(1)));
  EXPECT_EQ(0U, session()->GetTotalResourceCount());
  EXPECT_EQ(0U, session()->GetMappedResourceCount());
}

TEST_F(SessionTest, Labeling) {
  const ResourceId kNodeId = 1;
  const std::string kShortLabel = "test!";
  const std::string kLongLabel = std::string(::fuchsia::ui::gfx::kLabelMaxLength, 'x');
  const std::string kTooLongLabel = std::string(::fuchsia::ui::gfx::kLabelMaxLength + 1, '?');

  EXPECT_TRUE(Apply(scenic::NewCreateShapeNodeCmd(kNodeId)));
  auto shape_node = FindResource<ShapeNode>(kNodeId);
  EXPECT_TRUE(shape_node->label().empty());
  EXPECT_TRUE(Apply(scenic::NewSetLabelCmd(kNodeId, kShortLabel)));
  EXPECT_EQ(kShortLabel, shape_node->label());
  EXPECT_TRUE(Apply(scenic::NewSetLabelCmd(kNodeId, kLongLabel)));
  EXPECT_EQ(kLongLabel, shape_node->label());
  EXPECT_TRUE(Apply(scenic::NewSetLabelCmd(kNodeId, kTooLongLabel)));
  EXPECT_EQ(kTooLongLabel.substr(0, fuchsia::ui::gfx::kLabelMaxLength), shape_node->label());
  EXPECT_TRUE(Apply(scenic::NewSetLabelCmd(kNodeId, "")));
  EXPECT_TRUE(shape_node->label().empty());

  // Bypass the truncation performed by session helpers.
  shape_node->SetLabel(kTooLongLabel);
  EXPECT_EQ(kTooLongLabel.substr(0, fuchsia::ui::gfx::kLabelMaxLength), shape_node->label());
}

using BufferSessionTest = VkSessionTest;

VK_TEST_F(BufferSessionTest, BufferAliasing) {
  const size_t kVmoSize = 1024;
  const size_t kOffset = 512;

  auto vulkan_queues = CreateVulkanDeviceQueues();
  auto device = vulkan_queues->vk_device();
  auto physical_device = vulkan_queues->vk_physical_device();

  // TODO(fxbug.dev/24563): Scenic may use a different set of bits when creating a
  // buffer, resulting in a memory pool mismatch.
  const vk::BufferUsageFlags kUsageFlags =
      vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst |
      vk::BufferUsageFlagBits::eStorageTexelBuffer | vk::BufferUsageFlagBits::eStorageBuffer |
      vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eVertexBuffer;

  auto memory_requirements = GetBufferRequirements(device, kVmoSize, kUsageFlags);
  auto memory = AllocateExportableMemory(
      device, physical_device, memory_requirements,
      vk::MemoryPropertyFlagBits::eDeviceLocal | vk::MemoryPropertyFlagBits::eHostVisible);

  // If we can't make memory that is both host-visible and device-local, we
  // can't run this test.
  if (!memory) {
    device.freeMemory(memory);
    FX_LOGS(INFO) << "Could not find UMA compatible memory pool, aborting test.";
    return;
  }

  zx::vmo vmo = ExportMemoryAsVmo(device, vulkan_queues->dispatch_loader(), memory);

  zx::vmo dup_vmo;
  zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_vmo);
  ASSERT_EQ(ZX_OK, status);

  EXPECT_TRUE(Apply(scenic::NewCreateMemoryCmd(1, std::move(dup_vmo), kVmoSize,
                                               fuchsia::images::MemoryType::HOST_MEMORY)));
  EXPECT_TRUE(Apply(scenic::NewCreateBufferCmd(2, 1, 0, kVmoSize)));
  EXPECT_TRUE(Apply(scenic::NewCreateBufferCmd(3, 1, kOffset, kVmoSize - kOffset)));

  auto base_buffer = FindResource<Buffer>(2);
  auto offset_buffer = FindResource<Buffer>(3);

  EXPECT_TRUE(base_buffer);
  EXPECT_TRUE(base_buffer->escher_buffer());
  EXPECT_TRUE(base_buffer->escher_buffer()->host_ptr());

  EXPECT_TRUE(offset_buffer);
  EXPECT_TRUE(offset_buffer->escher_buffer());
  EXPECT_TRUE(offset_buffer->escher_buffer()->host_ptr());

  auto shared_vmo =
      fxl::AdoptRef(new fsl::SharedVmo(std::move(vmo), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE));
  uint8_t* raw_memory = static_cast<uint8_t*>(shared_vmo->Map());
  EXPECT_TRUE(raw_memory);

  memset(raw_memory, 0, kVmoSize);
  raw_memory[kOffset] = 1;
  EXPECT_EQ(base_buffer->escher_buffer()->host_ptr()[0], 0);
  EXPECT_EQ(base_buffer->escher_buffer()->host_ptr()[kOffset], 1);
  EXPECT_EQ(offset_buffer->escher_buffer()->host_ptr()[0], 1);
  device.freeMemory(memory);
}

TEST_F(SessionTest, RegisterBufferCollectionWithId0) {
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;

  session()->RegisterBufferCollection(0, std::move(token));
  ExpectLastReportedError("RegisterBufferCollection called with buffer_collection_id 0.");
}

TEST_F(SessionTest, RegisterBufferCollectionInvalid) {
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;

  session()->RegisterBufferCollection(1, std::move(token));
  ExpectLastReportedError("Unable to register collection.");
}

TEST_F(SessionTest, DeregisterBufferCollectionWithId0) {
  session()->DeregisterBufferCollection(0);
  ExpectLastReportedError("DeregisterBufferCollection called with buffer_collection_id 0.");
}

TEST_F(SessionTest, DeregisterBufferCollectionInexistent) {
  session()->DeregisterBufferCollection(1);
  ExpectLastReportedError("DeregisterBufferCollection failed, buffer_collection_id 1 not found.");
}

// TODO:
// - test that FindResource() cannot return resources that have the wrong
// type.

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
