// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/shadertoy/client/example_scene.h"

namespace shadertoy_client {

ExampleScene::ExampleScene(mozart::client::Session* session,
                           const mozart::client::Material& material,
                           float width,
                           float height)
    : width_(width),
      height_(height),
      renderer_(session),
      shape_(session, 640, 480, 80, 80, 80, 80),
      background_(session),
      rect0_(session),
      rect1_(session),
      rect2_(session),
      rect3_(session),
      rect4_(session),
      rect5_(session),
      rect6_(session),
      rect7_(session),
      rect8_(session),
      rect9_(session) {
  mozart::client::Scene scene(session);
  renderer_.SetCamera(mozart::client::Camera(scene));

  mozart::client::EntityNode root(session);
  scene.AddChild(root);

  mozart::client::Rectangle background_shape(session, width, height);
  mozart::client::Material background_material(session);
  background_material.SetColor(125, 179, 204, 1.0);

  background_.SetShape(background_shape);
  background_.SetMaterial(background_material);
  background_.SetTranslation(width * 0.5f, height * 0.5f, 0.f);

  root.AddChild(background_);
  root.AddChild(rect0_);
  root.AddChild(rect1_);
  root.AddChild(rect2_);
  root.AddChild(rect3_);
  root.AddChild(rect4_);
  root.AddChild(rect5_);
  root.AddChild(rect6_);
  root.AddChild(rect7_);
  root.AddChild(rect8_);
  root.AddChild(rect9_);

  rect0_.SetShape(shape_);
  rect0_.SetMaterial(material);
  rect1_.SetShape(shape_);
  rect1_.SetMaterial(material);
  rect2_.SetShape(shape_);
  rect2_.SetMaterial(material);
  rect3_.SetShape(shape_);
  rect3_.SetMaterial(material);
  rect4_.SetShape(shape_);
  rect4_.SetMaterial(material);
  rect5_.SetShape(shape_);
  rect5_.SetMaterial(material);
  rect6_.SetShape(shape_);
  rect6_.SetMaterial(material);
  rect7_.SetShape(shape_);
  rect7_.SetMaterial(material);
  rect8_.SetShape(shape_);
  rect8_.SetMaterial(material);
  rect9_.SetShape(shape_);
  rect9_.SetMaterial(material);
}

void ExampleScene::Update(float seconds) {
  mozart::client::ShapeNode* shapes[] = {&rect0_, &rect1_, &rect2_, &rect3_,
                                         &rect4_, &rect5_, &rect6_, &rect7_,
                                         &rect8_, &rect9_};

  const float kCenterX = width_ * 0.5f;
  const float kCenterY = height_ * 0.5f;

  for (int i = 0; i < 10; ++i) {
    float secs = seconds + (2.0 * i);
    shapes[i]->SetTranslation(kCenterX + sin(secs * 0.8) * 500.f,
                              kCenterY + sin(secs * 0.6) * 570.f, 2.0 + i);
  }
}

}  // namespace shadertoy_client
