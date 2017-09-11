// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "examples/waterfall/scenes/app_test_scene.h"

#include "escher/geometry/tessellation.h"
#include "escher/gl/gles2/bindings.h"
#include "escher/renderer.h"
#include "lib/fxl/arraysize.h"

using namespace escher;

namespace {

constexpr float kFabSize = 56.0f;

}  // namespace

AppTestScene::AppTestScene() {
  app_bar_material_.set_color(
      MakeConstantBinding(vec4(0.0f, 0.0f, 1.0f, 1.0f)));
  canvas_material_.set_color(MakeConstantBinding(vec4(1.0f, 1.0f, 1.0f, 1.0f)));
  card_material_.set_color(MakeConstantBinding(vec4(1.0f, 1.0f, 0.8f, 1.0f)));
  fab_material_.set_color(MakeConstantBinding(vec4(1.0f, 0.0f, 0.0f, 1.0f)));
  green_material_.set_color(MakeConstantBinding(vec4(0.0f, 1.0f, 0.0f, 1.0f)));
  // Rotate by 10 degrees and scale by 2.
  constexpr double angle = 3.14159265359 / 18;
  const float c = 5 * cos(angle);
  const float s = 5 * sin(angle);
  checkerboard_material_.set_texture_matrix(
      MakeConstantBinding(mat2(vec2(c, -s), vec2(s, c))));
}

AppTestScene::~AppTestScene() {}

void AppTestScene::InitGL() {
  // Generate RGB texture containing 2x2 checkerboard.
  constexpr GLubyte checkerboard[] = {255, 255, 255, 255, 0,   0,   0,   255,
                                      0,   0,   0,   255, 255, 255, 255, 255};
  GLuint texture = 0;
  glGenTextures(1, &texture);
  FXL_DCHECK(texture != 0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               &(checkerboard[0]));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  checkerboard_material_.set_texture(texture);

  circle_mesh_ = fxl::MakeRefCounted<escher::Mesh>(
      TessellateCircle(3, vec2(0.f, 0.f), 50.f));
}

Model AppTestScene::GetModel(const ViewingVolume& volume, const vec2& focus) {
  std::vector<Object> objects;

  // canvas
  objects.emplace_back(
      Shape::CreateRect(vec2(0.0f, 0.0f), vec2(volume.width(), volume.height()),
                        0),
      &canvas_material_);

  // app bar
  objects.emplace_back(
      Shape::CreateRect(vec2(0.0f, 0.0f), vec2(volume.width(), 56.0f), 4),
      &app_bar_material_);

  // card
  objects.emplace_back(
      Shape::CreateRect(vec2(0.0f, 200.0f), vec2(volume.width(), 120.0f), 2),
      &card_material_);

  // left eye
  objects.emplace_back(
      Shape::CreateRect(vec2(25.0f, 180.0f), vec2(60.0f, 40.0f), 10.0f),
      &green_material_);

  // right eye
  objects.emplace_back(
      Shape::CreateRect(vec2(125.0f, 180.0f), vec2(60.0f, 40.0f), 16.0f),
      &green_material_);

  // third eye
  objects.emplace_back(
      Shape::CreateRect(vec2(225.0f, 180.0f), vec2(60.0f, 40.0f), 5.0f),
      &checkerboard_material_);

  // fourth eye
  objects.emplace_back(
      Shape::CreateRect(vec2(325.0f, 180.0f), vec2(60.0f, 40.0f), 12.0f),
      &checkerboard_material_);

  // fifth eye
  objects.emplace_back(
      Shape::CreateRect(vec2(425.0f, 180.0f), vec2(60.0f, 40.0f), 19.0f),
      &checkerboard_material_);

  // sixth eye
  objects.emplace_back(
      Shape::CreateRect(vec2(0.0f, 245.0f), vec2(680.0f, 50.0f), 2.0f),
      &green_material_);

  // meshes
  objects.emplace_back(
      Shape::CreateMesh(circle_mesh_, vec2(650.f, 180.f), 20.0f),
      &app_bar_material_);

  // horizontal line segments
  objects.emplace_back(
      Shape::CreateRect(vec2(40.0f, 270.0f), vec2(40.0f, 1.0f), 2.0f),
      &canvas_material_);
  objects.emplace_back(
      Shape::CreateRect(vec2(100.0f, 270.0f), vec2(40.0f, 1.0f), 5.0f),
      &canvas_material_);
  objects.emplace_back(
      Shape::CreateRect(vec2(160.0f, 270.0f), vec2(40.0f, 1.0f), 9.0f),
      &canvas_material_);
  objects.emplace_back(
      Shape::CreateRect(vec2(220.0f, 270.0f), vec2(40.0f, 1.0f), 13.0f),
      &canvas_material_);

  // vertical line segments
  objects.emplace_back(
      Shape::CreateRect(vec2(290.0f, 250.0f), vec2(1.0f, 40.0f), 2.0f),
      &canvas_material_);
  objects.emplace_back(
      Shape::CreateRect(vec2(330.0f, 250.0f), vec2(1.0f, 40.0f), 5.0f),
      &canvas_material_);
  objects.emplace_back(
      Shape::CreateRect(vec2(370.0f, 250.0f), vec2(1.0f, 40.0f), 9.0f),
      &canvas_material_);
  objects.emplace_back(
      Shape::CreateRect(vec2(410.0f, 250.0f), vec2(1.0f, 40.0f), 13.0f),
      &canvas_material_);

  // crossed line segments
  objects.emplace_back(
      Shape::CreateRect(vec2(440.0f, 270.0f), vec2(40.0f, 1.0f), 2.0f),
      &canvas_material_);
  objects.emplace_back(
      Shape::CreateRect(vec2(500.0f, 270.0f), vec2(40.0f, 1.0f), 5.0f),
      &canvas_material_);
  objects.emplace_back(
      Shape::CreateRect(vec2(560.0f, 270.0f), vec2(40.0f, 1.0f), 9.0f),
      &canvas_material_);
  objects.emplace_back(
      Shape::CreateRect(vec2(620.0f, 270.0f), vec2(40.0f, 1.0f), 13.0f),
      &canvas_material_);
  objects.emplace_back(
      Shape::CreateRect(vec2(460.0f, 250.0f), vec2(1.0f, 40.0f), 2.0f),
      &canvas_material_);
  objects.emplace_back(
      Shape::CreateRect(vec2(520.0f, 250.0f), vec2(1.0f, 40.0f), 5.0f),
      &canvas_material_);
  objects.emplace_back(
      Shape::CreateRect(vec2(580.0f, 250.0f), vec2(1.0f, 40.0f), 9.0f),
      &canvas_material_);
  objects.emplace_back(
      Shape::CreateRect(vec2(640.0f, 250.0f), vec2(1.0f, 40.0f), 13.0f),
      &canvas_material_);

  // null
  objects.emplace_back(
      Shape::CreateRect(vec2(40.0f, 310.0f), vec2(40.0f, 40.0f), 2.0f),
      &null_material_);

  // fabs
  objects.emplace_back(
      Shape::CreateCircle(focus - vec2(kFabSize, 0.f), kFabSize / 2.0f, 4.0f),
      &fab_material_);
  objects.emplace_back(
      Shape::CreateCircle(focus + vec2(kFabSize, 0.f), kFabSize / 2.0f, 12.0f),
      &fab_material_);

  return Model(std::move(objects));
}
