// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "examples/waterfall/scenes/app_test_scene.h"

#include "escher/gl/bindings.h"
#include "escher/renderer.h"
#include "ftl/arraysize.h"

namespace {

constexpr float kFabSize = 56.0f;

}  // namespace

AppTestScene::AppTestScene() {
  app_bar_material_.set_color(
      escher::MakeConstantBinding(glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)));
  canvas_material_.set_color(
      escher::MakeConstantBinding(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)));
  card_material_.set_color(
      escher::MakeConstantBinding(glm::vec4(1.0f, 1.0f, 0.8f, 1.0f)));
  fab_material_.set_color(
      escher::MakeConstantBinding(glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)));
  green_material_.set_color(
      escher::MakeConstantBinding(glm::vec4(0.0f, 1.0f, 0.0f, 1.0f)));
  // Rotate by 10 degrees and scale by 2.
  constexpr double angle = 3.14159265359 / 18;
  const float c = 5 * cos(angle);
  const float s = 5 * sin(angle);
  checkerboard_material_.set_texture_matrix(
      escher::MakeConstantBinding(
          glm::mat2(glm::vec2(c, -s), glm::vec2(s, c))));
}

AppTestScene::~AppTestScene() {}

void AppTestScene::InitGL() {
  // Generate RGB texture containing 2x2 checkerboard.
  constexpr GLubyte checkerboard[] =
      {255, 255, 255, 255, 0, 0, 0, 255,
       0, 0, 0, 255, 255, 255, 255, 255};
  GLuint texture = 0;
  glGenTextures(1, &texture);
  FTL_DCHECK(texture != 0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE,
      &(checkerboard[0]));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  checkerboard_material_.set_texture(texture);
}

escher::Model AppTestScene::GetModel(const escher::ViewingVolume& volume,
                                     const glm::vec2& focus) {
  std::vector<escher::Object> objects;

  // canvas
  objects.emplace_back(
      escher::Shape::CreateRect(glm::vec2(0.0f, 0.0f),
                                glm::vec2(volume.width(), volume.height()),
                                0.0f),
      &canvas_material_);

  // app bar
  objects.emplace_back(
      escher::Shape::CreateRect(glm::vec2(0.0f, 0.0f),
                                glm::vec2(volume.width(), 56.0f), 4.0f),
      &app_bar_material_);

  // card
  objects.emplace_back(
      escher::Shape::CreateRect(glm::vec2(0.0f, 200.0f),
                                glm::vec2(volume.width(), 120.0f), 2.0f),
      &card_material_);

  // left eye
  objects.emplace_back(
      escher::Shape::CreateRect(glm::vec2(100.0f, 180.0f),
                                glm::vec2(60.0f, 40.0f), 10.0f),
      &green_material_);

  // right eye
  objects.emplace_back(
      escher::Shape::CreateRect(glm::vec2(200.0f, 180.0f),
                                glm::vec2(60.0f, 40.0f), 16.0f),
      &green_material_);

  // third eye
  objects.emplace_back(
      escher::Shape::CreateRect(glm::vec2(300.0f, 180.0f),
                                glm::vec2(60.0f, 40.0f), 5.0f),
      &checkerboard_material_);

  // fourth eye
  objects.emplace_back(
      escher::Shape::CreateRect(glm::vec2(400.0f, 180.0f),
                                glm::vec2(60.0f, 40.0f), 12.0f),
      &checkerboard_material_);

  // fifth eye
  objects.emplace_back(
      escher::Shape::CreateRect(glm::vec2(500.0f, 180.0f),
                                glm::vec2(60.0f, 40.0f), 19.0f),
      &checkerboard_material_);

  // null
  objects.emplace_back(escher::Shape::CreateRect(glm::vec2(20.0f, 290.0f),
                                                 glm::vec2(40.0f, 40.0f), 2.0f),
                       &null_material_);

  // fabs
  objects.emplace_back(
      escher::Shape::CreateCircle(focus - glm::vec2(kFabSize, 0.f), kFabSize / 2.0f, 4.0f),
      &fab_material_);
  objects.emplace_back(
      escher::Shape::CreateCircle(focus + glm::vec2(kFabSize, 0.f), kFabSize / 2.0f, 12.0f),
      &fab_material_);

  return escher::Model(std::move(objects));
}
