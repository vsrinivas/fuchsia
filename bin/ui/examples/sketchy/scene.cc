// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/sketchy/scene.h"

namespace sketchy_example {

Scene::Scene(mozart::client::Session* session, float width, float height)
    : compositor_(session),
      stroke_group_holder_(session) {

  mozart::client::Scene scene(session);
  mozart::client::Renderer renderer(session);
  renderer.SetCamera(mozart::client::Camera(scene));

  mozart::client::Layer layer(session);
  layer.SetRenderer(renderer);
  layer.SetSize(width, height);
  mozart::client::LayerStack layer_stack(session);
  layer_stack.AddLayer(layer);
  compositor_.SetLayerStack(layer_stack);

  mozart::client::EntityNode root(session);
  mozart::client::ShapeNode background_node(session);
  mozart::client::Rectangle background_shape(session, width, height);
  mozart::client::Material background_material(session);
  background_material.SetColor(220, 220, 220, 255);
  background_node.SetShape(background_shape);
  background_node.SetMaterial(background_material);
  background_node.SetTranslation(width * .5f, height * .5f, 0);

  scene.AddChild(root);
  root.AddChild(background_node);
  root.AddChild(stroke_group_holder_);
}

}  // namespace sketchy_example
