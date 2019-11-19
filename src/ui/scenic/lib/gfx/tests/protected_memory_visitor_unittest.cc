// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/protected_memory_visitor.h"

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/gfx/resources/image.h"
#include "src/ui/scenic/lib/gfx/resources/image_base.h"
#include "src/ui/scenic/lib/gfx/resources/material.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/entity_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/opacity_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/shape_node.h"
#include "src/ui/scenic/lib/gfx/tests/session_test.h"

namespace scenic_impl::gfx::test {

namespace {

// Dummy image that can be marked as protected.
class DummyImage : public ImageBase {
 public:
  DummyImage(Session* session, ResourceId id, bool use_protected_memory)
      : ImageBase(session, id, Image::kTypeInfo), use_protected_memory_(use_protected_memory) {}

  void Accept(class ResourceVisitor*) override {}

  void UpdateEscherImage(escher::BatchGpuUploader* gpu_uploader) override {}

  const escher::ImagePtr& GetEscherImage() override {
    static const escher::ImagePtr kNullEscherImage;
    return kNullEscherImage;
  }

  bool use_protected_memory() override { return use_protected_memory_; }

 private:
  bool use_protected_memory_;
};

}  // namespace

class ProtectedMemoryVisitorTest : public SessionTest {};

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

// TODO(before-submit): file bug to use View/ViewHolder.
/*
TEST_F(ProtectedMemoryVisitorTest, ReturnsTrueForImportedProtectedImage) {
  ProtectedMemoryVisitor visitor;

  ResourceId next_id = 1;
  MaterialPtr protected_material = fxl::MakeRefCounted<Material>(session(), next_id++);
  ImageBasePtr protected_image = fxl::AdoptRef(new DummyImage(session(), next_id++, true));
  protected_material->SetTexture(protected_image);
  auto shape_node = fxl::MakeRefCounted<ShapeNode>(session(), session()->id(), next_id++);
  shape_node->SetMaterial(protected_material);

  auto resource_linker = std::make_unique<ResourceLinker>();
  auto kImportSpec = fuchsia::ui::gfx::ImportSpec::NODE;
  auto import_node =
      fxl::MakeRefCounted<Import>(session(), next_id++, kImportSpec, resource_linker->GetWeakPtr());

  fxl::RefPtr<EntityNode> entity_node = import_node->delegate()->As<EntityNode>();
  ASSERT_TRUE(entity_node);
  entity_node->AddChild(shape_node, session()->error_reporter());

  visitor.Visit(import_node.get());
  ASSERT_TRUE(visitor.HasProtectedMemoryUse());
}
*/

}  // namespace scenic_impl::gfx::test
