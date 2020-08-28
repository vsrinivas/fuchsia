// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/snapshot/snapshotter.h"

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "lib/ui/scenic/cpp/commands.h"
#include "src/lib/fsl/vmo/vector.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/resources/resource_manager.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/scenic/lib/gfx/resources/image.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/entity_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/shape_node.h"
#include "src/ui/scenic/lib/gfx/snapshot/serializer.h"
#include "src/ui/scenic/lib/gfx/snapshot/version.h"
#include "src/ui/scenic/lib/gfx/tests/vk_session_test.h"

namespace scenic_impl {
namespace gfx {
namespace test {

// Dummy resource manager used for WrapVkImage.
class DummyResourceManager : public escher::ResourceManager {
 public:
  DummyResourceManager() : escher::ResourceManager(escher::EscherWeakPtr()) {}

  void OnReceiveOwnable(std::unique_ptr<escher::Resource> resource) override {}
};

class SnapshotterTest : public VkSessionTest {
 public:
  ResourceId kParentId = 1;
  ResourceId kMaterialId = 0;

  void SetUp() override {
    VkSessionTest::SetUp();

    ResourceId nextId = kParentId + 1;
    const ResourceId kChildId = nextId++;

    EXPECT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(kParentId)));
    EXPECT_TRUE(Apply(scenic::NewSetLabelCmd(kParentId, "Parent")));
    EXPECT_TRUE(Apply(scenic::NewCreateShapeNodeCmd(kChildId)));
    EXPECT_TRUE(Apply(scenic::NewAddChildCmd(kParentId, kChildId)));

    kMaterialId = nextId++;
    EXPECT_TRUE(Apply(scenic::NewCreateMaterialCmd(kMaterialId)));
    EXPECT_TRUE(Apply(scenic::NewSetTextureCmd(kMaterialId, 0)));
    EXPECT_TRUE(Apply(scenic::NewSetColorCmd(kMaterialId, 255, 100, 100, 255)));
    EXPECT_TRUE(Apply(scenic::NewSetMaterialCmd(kChildId, kMaterialId)));

    const ResourceId kShapeId = nextId++;
    EXPECT_TRUE(Apply(scenic::NewCreateCircleCmd(kShapeId, 50.f)));
    EXPECT_TRUE(Apply(scenic::NewSetShapeCmd(kChildId, kShapeId)));
  }

  DummyResourceManager resource_manager_;
};

VK_TEST_F(SnapshotterTest, Creation) {
  auto escher = escher::test::GetEscher()->GetWeakPtr();
  Snapshotter snapshotter(escher);

  auto entity = FindResource<EntityNode>(kParentId);
  ASSERT_NE(nullptr, entity.get());

  size_t size = 0;
  snapshotter.TakeSnapshot(entity.get(), [&size](::fuchsia::mem::Buffer buffer, bool success) {
    EXPECT_TRUE(success);

    size = buffer.size;
    std::vector<uint8_t> data;
    EXPECT_TRUE(fsl::VectorFromVmo(buffer, &data));

    // De-serialize the snapshot from flatbuffer.
    auto snapshot = (const SnapshotData*)data.data();

    // This test assumes flatbuffer snapshot format, version 1.
    EXPECT_EQ(SnapshotData::SnapshotType::kFlatBuffer, snapshot->type);
    EXPECT_EQ(SnapshotData::SnapshotVersion::v1_0, snapshot->version);

    auto node = flatbuffers::GetRoot<snapshot::Node>(snapshot->data);
    EXPECT_EQ("Parent", node->name()->str());

    EXPECT_EQ(1u, node->children()->size());
    auto child = node->children()->Get(0);

    EXPECT_EQ(snapshot::Shape_Circle, child->shape_type());
    auto shape = static_cast<const snapshot::Circle*>(child->shape());
    EXPECT_EQ(50.f, shape->radius());

    EXPECT_EQ(snapshot::Material_Color, child->material_type());
    auto color = static_cast<const snapshot::Color*>(child->material());
    EXPECT_EQ(255, static_cast<uint8_t>(color->red() * 255.f + 0.5f));
  });

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(size > 0);
}

// Dummy image that is marked as protected.
class DummyProtectedImage : public Image {
 public:
  DummyProtectedImage(escher::EscherWeakPtr escher, Session* session,
                      escher::ResourceManager* resource_manager, ResourceId id,
                      bool use_protected_memory)
      : Image(session, id, Image::kTypeInfo) {
    uint8_t kColors[] = {kRedValue, kGreenValue, kBlueValue, kAlphaValue};
    escher::BatchGpuUploader uploader(escher);
    image_ = escher->NewRgbaImage(&uploader, 1, 1, kColors);
    uploader.Submit();
    escher->vk_device().waitIdle();

    if (use_protected_memory) {
      auto image_info = image_->info();
      image_info.memory_flags = vk::MemoryPropertyFlagBits::eProtected;
      image_ = escher::Image::WrapVkImage(resource_manager, image_info, image_->vk(),
                                          vk::ImageLayout::eUndefined);
    }
  }

  void Accept(ResourceVisitor* visitor) override { visitor->Visit(this); }

  const escher::ImagePtr& GetEscherImage() override { return image_; }

  const uint8_t kRedValue = 2;
  const uint8_t kGreenValue = 3;
  const uint8_t kBlueValue = 4;
  const uint8_t kAlphaValue = 5;

 private:
  bool UpdatePixels(escher::BatchGpuUploader* gpu_uploader) override { return true; }

  escher::ImagePtr image_;
};

VK_TEST_F(SnapshotterTest, NonProtectedImage) {
  auto escher = escher::test::GetEscher()->GetWeakPtr();
  Snapshotter snapshotter(escher);

  auto material = FindResource<Material>(kMaterialId);
  ASSERT_NE(nullptr, material.get());
  auto dummy_image =
      fxl::MakeRefCounted<DummyProtectedImage>(escher, session(), &resource_manager_, 123,
                                               /*use_protected_memory=*/false);
  material->SetTexture(dummy_image);

  auto entity = FindResource<EntityNode>(kParentId);
  ASSERT_NE(nullptr, entity.get());
  snapshotter.TakeSnapshot(entity.get(),
                           [dummy_image](::fuchsia::mem::Buffer buffer, bool success) {
                             EXPECT_TRUE(success);
                             EXPECT_TRUE(buffer.size > 0);

                             std::vector<uint8_t> data;
                             EXPECT_TRUE(fsl::VectorFromVmo(buffer, &data));
                             // De-serialize the snapshot from flatbuffer.
                             auto snapshot = (const SnapshotData*)data.data();
                             auto node = flatbuffers::GetRoot<snapshot::Node>(snapshot->data);

                             // Expect Image to contain same values as constructed.
                             auto child = node->children()->Get(0);
                             EXPECT_EQ(snapshot::Material_Image, child->material_type());
                             auto image = static_cast<const snapshot::Image*>(child->material());
                             EXPECT_TRUE(image->data()->Length() > 0);
                             EXPECT_EQ(dummy_image->kRedValue, image->data()->Data()[0]);
                             EXPECT_EQ(dummy_image->kGreenValue, image->data()->Data()[1]);
                             EXPECT_EQ(dummy_image->kBlueValue, image->data()->Data()[2]);
                             EXPECT_EQ(dummy_image->kAlphaValue, image->data()->Data()[3]);
                           });

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
}

VK_TEST_F(SnapshotterTest, ProtectedImage) {
  auto escher = escher::test::GetEscher()->GetWeakPtr();
  Snapshotter snapshotter(escher);

  auto material = FindResource<Material>(kMaterialId);
  ASSERT_NE(nullptr, material.get());
  auto dummy_image =
      fxl::MakeRefCounted<DummyProtectedImage>(escher, session(), &resource_manager_, 123,
                                               /*use_protected_memory=*/true);
  ASSERT_TRUE(dummy_image->GetEscherImage()->use_protected_memory());
  material->SetTexture(dummy_image);

  auto entity = FindResource<EntityNode>(kParentId);
  ASSERT_NE(nullptr, entity.get());
  snapshotter.TakeSnapshot(entity.get(),
                           [dummy_image](::fuchsia::mem::Buffer buffer, bool success) {
                             EXPECT_TRUE(success);
                             EXPECT_TRUE(buffer.size > 0);

                             std::vector<uint8_t> data;
                             EXPECT_TRUE(fsl::VectorFromVmo(buffer, &data));
                             // De-serialize the snapshot from flatbuffer.
                             auto snapshot = (const SnapshotData*)data.data();
                             auto node = flatbuffers::GetRoot<snapshot::Node>(snapshot->data);

                             // Expect Image to be replaced by black content.
                             auto child = node->children()->Get(0);
                             EXPECT_EQ(snapshot::Material_Image, child->material_type());
                             auto image = static_cast<const snapshot::Image*>(child->material());
                             EXPECT_TRUE(image->data()->Length() > 0);
                             EXPECT_EQ(0, image->data()->Data()[0]);
                             EXPECT_EQ(0, image->data()->Data()[1]);
                             EXPECT_EQ(0, image->data()->Data()[2]);
                             EXPECT_EQ(255, image->data()->Data()[3]);
                           });

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
