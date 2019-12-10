// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/protected_memory_visitor.h"

#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/gfx/resources/image.h"
#include "src/ui/scenic/lib/gfx/resources/image_base.h"
#include "src/ui/scenic/lib/gfx/resources/material.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/entity_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/opacity_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/shape_node.h"
#include "src/ui/scenic/lib/gfx/resources/view_holder.h"
#include "src/ui/scenic/lib/gfx/tests/session_test.h"

namespace scenic_impl::gfx::test {

namespace {

// Dummy image that can be marked as protected.
class DummyImage : public ImageBase {
 public:
  DummyImage(Session* session, ResourceId id, bool use_protected_memory)
      : ImageBase(session, id, Image::kTypeInfo), use_protected_memory_(use_protected_memory) {}

  void Accept(class ResourceVisitor*) override {}

  void UpdateEscherImage(escher::BatchGpuUploader* gpu_uploader,
                         escher::ImageLayoutUpdater* layout_updater) override {}

  const escher::ImagePtr& GetEscherImage() override {
    static const escher::ImagePtr kNullEscherImage;
    return kNullEscherImage;
  }

  bool use_protected_memory() override { return use_protected_memory_; }

 private:
  bool use_protected_memory_;
};

}  // namespace

class ProtectedMemoryVisitorTest : public SessionTest {
 public:
  ProtectedMemoryVisitorTest() {}

  void TearDown() override {
    SessionTest::TearDown();

    view_linker_.reset();
  }

  SessionContext CreateSessionContext() override {
    SessionContext session_context = SessionTest::CreateSessionContext();

    FXL_DCHECK(!view_linker_);

    view_linker_ = std::make_unique<ViewLinker>();
    session_context.view_linker = view_linker_.get();

    return session_context;
  }

  std::unique_ptr<ViewLinker> view_linker_;
};

TEST_F(ProtectedMemoryVisitorTest, ReturnsFalseForOpacityNode) {
  ProtectedMemoryVisitor visitor;

  ResourceId next_id = 1;
  auto opacity_node = fxl::MakeRefCounted<OpacityNode>(session(), session()->id(), next_id++);
  visitor.Visit(opacity_node.get());
  ASSERT_FALSE(visitor.HasProtectedMemoryUse());
}

TEST_F(ProtectedMemoryVisitorTest, ReturnsTrueForProtectedImage) {
  ProtectedMemoryVisitor visitor;

  ResourceId next_id = 1;
  MaterialPtr image_material = fxl::MakeRefCounted<Material>(session(), next_id++);
  ImageBasePtr image = fxl::AdoptRef(new DummyImage(session(), next_id++, false));
  image_material->SetTexture(image);

  visitor.Visit(image_material.get());
  ASSERT_FALSE(visitor.HasProtectedMemoryUse());

  MaterialPtr protected_material = fxl::MakeRefCounted<Material>(session(), next_id++);
  ImageBasePtr protected_image = fxl::AdoptRef(new DummyImage(session(), next_id++, true));
  protected_material->SetTexture(protected_image);

  visitor.Visit(protected_material.get());
  ASSERT_TRUE(visitor.HasProtectedMemoryUse());
}

TEST_F(ProtectedMemoryVisitorTest, ReturnsTrueForChildProtectedImage) {
  ProtectedMemoryVisitor visitor;

  ResourceId next_id = 1;
  MaterialPtr protected_material = fxl::MakeRefCounted<Material>(session(), next_id++);
  ImageBasePtr protected_image = fxl::AdoptRef(new DummyImage(session(), next_id++, true));
  protected_material->SetTexture(protected_image);

  auto shape_node = fxl::MakeRefCounted<ShapeNode>(session(), session()->id(), next_id++);
  shape_node->SetMaterial(protected_material);

  auto opacity_node = fxl::MakeRefCounted<OpacityNode>(session(), session()->id(), next_id++);
  opacity_node->AddChild(shape_node, session()->error_reporter());

  visitor.Visit(opacity_node.get());
  ASSERT_TRUE(visitor.HasProtectedMemoryUse());
}

TEST_F(ProtectedMemoryVisitorTest, ReturnsTrueForProtectedImageInAView) {
  ProtectedMemoryVisitor visitor;

  ResourceId next_id = 1;
  const ResourceId view_holder_id = next_id++;
  const ResourceId view_id = next_id++;
  const ResourceId node_id = next_id++;
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  EXPECT_TRUE(Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token),
                                                   "test_view_holder")));
  EXPECT_TRUE(Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "test_view")));
  EXPECT_TRUE(Apply(scenic::NewCreateShapeNodeCmd(node_id)));
  EXPECT_TRUE(Apply(scenic::NewAddChildCmd(view_id, node_id)));
  EXPECT_ERROR_COUNT(0);
  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  auto view = FindResource<View>(view_id);
  auto shape_node = FindResource<ShapeNode>(node_id);

  EXPECT_TRUE(Apply(scenic::NewAddChildCmd(view_id, node_id)));
  EXPECT_ERROR_COUNT(0);

  MaterialPtr protected_material = fxl::MakeRefCounted<Material>(session(), next_id++);
  ImageBasePtr protected_image = fxl::AdoptRef(new DummyImage(session(), next_id++, true));
  protected_material->SetTexture(protected_image);
  shape_node->SetMaterial(protected_material);

  visitor.Visit(view_holder.get());
  ASSERT_TRUE(visitor.HasProtectedMemoryUse());
}

}  // namespace scenic_impl::gfx::test
