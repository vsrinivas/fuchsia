// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/snapshot/snapshotter.h"
#include "garnet/lib/ui/gfx/resources/nodes/entity_node.h"
#include "garnet/lib/ui/gfx/resources/nodes/shape_node.h"
#include "garnet/lib/ui/gfx/resources/snapshot/serializer.h"
#include "garnet/lib/ui/gfx/resources/snapshot/version.h"
#include "garnet/lib/ui/gfx/tests/vk_session_test.h"
#include "src/ui/lib/escher/test/gtest_escher.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "lib/fsl/vmo/vector.h"
#include "src/lib/fxl/logging.h"
#include "lib/ui/scenic/cpp/commands.h"

#include "gtest/gtest.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class SnapshotterTest : public VkSessionTest {
 public:
  int kParentId = 1;

  void SetUp() override {
    VkSessionTest::SetUp();

    int nextId = kParentId + 1;
    const ResourceId kChildId = nextId++;

    EXPECT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(kParentId)));
    EXPECT_TRUE(Apply(scenic::NewSetLabelCmd(kParentId, "Parent")));
    EXPECT_TRUE(Apply(scenic::NewCreateShapeNodeCmd(kChildId)));
    EXPECT_TRUE(Apply(scenic::NewAddPartCmd(kParentId, kChildId)));

    const ResourceId kMaterialId = nextId++;
    EXPECT_TRUE(Apply(scenic::NewCreateMaterialCmd(kMaterialId)));
    EXPECT_TRUE(Apply(scenic::NewSetTextureCmd(kMaterialId, 0)));
    EXPECT_TRUE(Apply(scenic::NewSetColorCmd(kMaterialId, 255, 100, 100, 255)));
    EXPECT_TRUE(Apply(scenic::NewSetMaterialCmd(kChildId, kMaterialId)));

    const ResourceId kShapeId = nextId++;
    EXPECT_TRUE(Apply(scenic::NewCreateCircleCmd(kShapeId, 50.f)));
    EXPECT_TRUE(Apply(scenic::NewSetShapeCmd(kChildId, kShapeId)));
  }
};

VK_TEST_F(SnapshotterTest, DISABLED_Creation) {
  auto escher = escher::test::GetEscher()->GetWeakPtr();
  Snapshotter snapshotter(escher::BatchGpuUploader::New(escher));

  auto entity = FindResource<EntityNode>(kParentId);
  ASSERT_NE(nullptr, entity.get());

  size_t size = 0;
  snapshotter.TakeSnapshot(
      entity.get(), [&size](::fuchsia::mem::Buffer buffer) {
        size = buffer.size;
        std::vector<uint8_t> data;
        EXPECT_TRUE(fsl::VectorFromVmo(buffer, &data));

        // De-serialize the snapshot from flatbuffer.
        auto snapshot = (const SnapshotData *)data.data();

        // This test assumes flatbuffer snapshot format, version 1.
        EXPECT_EQ(SnapshotData::SnapshotType::kFlatBuffer, snapshot->type);
        EXPECT_EQ(SnapshotData::SnapshotVersion::v1_0, snapshot->version);

        auto node = flatbuffers::GetRoot<snapshot::Node>(snapshot->data);
        EXPECT_EQ("Parent", node->name()->str());

        EXPECT_EQ(1u, node->children()->size());
        auto child = node->children()->Get(0);

        EXPECT_EQ(snapshot::Shape_Circle, child->shape_type());
        auto shape = static_cast<const snapshot::Circle *>(child->shape());
        EXPECT_EQ(50.f, shape->radius());

        EXPECT_EQ(snapshot::Material_Color, child->material_type());
        auto color = static_cast<const snapshot::Color *>(child->material());
        EXPECT_EQ(1.0f, color->red());
      });

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(size > 0);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
