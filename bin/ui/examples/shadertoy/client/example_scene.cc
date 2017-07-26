// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/shadertoy/client/example_scene.h"

namespace shadertoy_client {

ExampleScene::ExampleScene(mozart::client::Session* session,
                           const mozart::client::Material& material,
                           float scene_width,
                           float scene_height,
                           float rect_width,
                           float rect_height)
    : width_(scene_width),
      height_(scene_height),
      renderer_(session),
      shape_(session, rect_width, rect_height, 80, 80, 80, 80),
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
      rect9_(session),
      rect10_(session),
      rect11_(session),
      rect12_(session),
      rect13_(session),
      rect14_(session),
      rect15_(session) {
  mozart::client::Scene scene(session);
  renderer_.SetCamera(mozart::client::Camera(scene));

  mozart::client::EntityNode root(session);
  scene.AddChild(root);

  mozart::client::Rectangle background_shape(session, width_, height_);
  mozart::client::Material background_material(session);
  background_material.SetColor(125, 179, 204, 1.0);

  background_.SetShape(background_shape);
  background_.SetMaterial(background_material);
  background_.SetTranslation(width_ * 0.5f, height_ * 0.5f, 0.f);

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
  root.AddChild(rect10_);
  root.AddChild(rect11_);
  root.AddChild(rect12_);
  root.AddChild(rect13_);
  root.AddChild(rect14_);
  root.AddChild(rect15_);

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
  rect10_.SetShape(shape_);
  rect10_.SetMaterial(material);
  rect11_.SetShape(shape_);
  rect11_.SetMaterial(material);
  rect12_.SetShape(shape_);
  rect12_.SetMaterial(material);
  rect13_.SetShape(shape_);
  rect13_.SetMaterial(material);
  rect14_.SetShape(shape_);
  rect14_.SetMaterial(material);
  rect15_.SetShape(shape_);
  rect15_.SetMaterial(material);
}

void ExampleScene::Update(float seconds) {
  mozart::client::ShapeNode* shapes[] = {
      &rect0_,  &rect1_,  &rect2_,  &rect3_, &rect4_,  &rect5_,
      &rect6_,  &rect7_,  &rect8_,  &rect9_, &rect10_, &rect11_,
      &rect12_, &rect13_, &rect14_, &rect15_};

  const float kHalfWidth = width_ * 0.5f;
  const float kHalfHeight = height_ * 0.5f;

  for (int i = 0; i < 16; ++i) {
    // Each rect has a slightly different speed.
    float animation_time = seconds * (32 + i) / 32.f;
    shapes[i]->SetTranslation(
        kHalfWidth + sin(animation_time * 0.8) * kHalfWidth * 0.8,
        kHalfHeight + sin(animation_time * 0.6) * kHalfHeight * 0.9, 2.0 + i);
  }
}

}  // namespace shadertoy_client
