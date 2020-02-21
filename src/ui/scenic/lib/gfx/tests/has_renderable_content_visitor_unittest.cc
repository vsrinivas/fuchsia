// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/has_renderable_content_visitor.h"

#include <lib/ui/scenic/cpp/commands.h>

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/gfx/resources/camera.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"
#include "src/ui/scenic/lib/gfx/resources/material.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/entity_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/scene.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/shape_node.h"
#include "src/ui/scenic/lib/gfx/resources/renderers/renderer.h"
#include "src/ui/scenic/lib/gfx/tests/session_test.h"

namespace scenic_impl::gfx::test {

using HasRenderableContentUnittest = SessionTest;

TEST_F(HasRenderableContentUnittest, ReturnsTrueForShapeNodeWithMaterial) {
  HasRenderableContentVisitor visitor;

  ResourceId next_id = 1;
  auto layer = fxl::MakeRefCounted<Layer>(session(), session()->id(), next_id++);
  auto renderer = fxl::MakeRefCounted<Renderer>(session(), session()->id(), next_id++);
  layer->SetRenderer(renderer);
  auto scene =
      fxl::MakeRefCounted<Scene>(session(), session()->id(), next_id++,
                                 fxl::WeakPtr<ViewTreeUpdater>(), event_reporter()->GetWeakPtr());
  auto camera = fxl::MakeRefCounted<Camera>(session(), session()->id(), next_id++, scene);
  renderer->SetCamera(camera);
  auto node = fxl::MakeRefCounted<EntityNode>(session(), session()->id(), next_id++);
  scene->AddChild(node, error_reporter());
  auto shape_node = fxl::MakeRefCounted<ShapeNode>(session(), session()->id(), next_id++);
  node->AddChild(shape_node, error_reporter());

  visitor.Visit(layer.get());
  EXPECT_FALSE(visitor.HasRenderableContent());

  auto material = fxl::MakeRefCounted<Material>(session(), next_id++);
  shape_node->SetMaterial(material);

  visitor.Visit(layer.get());
  EXPECT_TRUE(visitor.HasRenderableContent());
}

}  // namespace scenic_impl::gfx::test
