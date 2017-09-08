// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/sketchy/scene.h"

namespace sketchy_example {

Scene::Scene(scenic_lib::Session* session, float width, float height)
    : compositor_(session), stroke_group_holder_(session) {
  scenic_lib::Scene scene(session);
  scenic_lib::Renderer renderer(session);
  renderer.SetCamera(scenic_lib::Camera(scene));

  scenic_lib::Layer layer(session);
  layer.SetRenderer(renderer);
  layer.SetSize(width, height);
  scenic_lib::LayerStack layer_stack(session);
  layer_stack.AddLayer(layer);
  compositor_.SetLayerStack(layer_stack);

  scenic_lib::EntityNode root(session);
  scenic_lib::ShapeNode background_node(session);
  scenic_lib::Rectangle background_shape(session, width, height);
  scenic_lib::Material background_material(session);
  background_material.SetColor(220, 220, 220, 255);
  background_node.SetShape(background_shape);
  background_node.SetMaterial(background_material);
  background_node.SetTranslation(width * .5f, height * .5f, 0);

  scene.AddChild(root);
  root.AddChild(background_node);
  root.AddChild(stroke_group_holder_);
}

}  // namespace sketchy_example
