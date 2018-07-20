// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/escher/waterfall/scenes/wobbly_ocean_scene.h"

#include "lib/escher/geometry/tessellation.h"
#include "lib/escher/geometry/types.h"
#include "lib/escher/material/color_utils.h"
#include "lib/escher/material/material.h"
#include "lib/escher/scene/model.h"
#include "lib/escher/scene/stage.h"
#include "lib/escher/shape/modifier_wobble.h"
#include "lib/escher/util/stopwatch.h"
#include "lib/escher/vk/image.h"
#include "lib/escher/vk/vulkan_context.h"

using escher::MeshAttribute;
using escher::MeshSpec;
using escher::Object;
using escher::ShapeModifier;
using escher::TexturePtr;
using escher::vec2;
using escher::vec3;

WobblyOceanScene::WobblyOceanScene(Demo* demo) : Scene(demo) {}

void WobblyOceanScene::Init(escher::Stage* stage) {
  bg_ = fxl::MakeRefCounted<escher::Material>();
  color1_ = fxl::MakeRefCounted<escher::Material>();
  color2_ = fxl::MakeRefCounted<escher::Material>();
  color3_ = fxl::MakeRefCounted<escher::Material>();
  color4_ = fxl::MakeRefCounted<escher::Material>();

  bg_->set_color(vec3(0.8f, 0.8f, 0.8f));
  color1_->set_color(vec3(63.f / 255.f, 138.f / 255.f, 153.f / 255.f));
  color2_->set_color(vec3(143.f / 255.f, 143.f / 255.f, 143.f / 255.f));
  color3_->set_color(escher::SrgbToLinear(vec3(0.913f, 0.384f, 0.352f)));
  color4_->set_color(escher::SrgbToLinear(vec3(0.286f, 0.545f, 0.607f)));

  TexturePtr checkerboard = escher()->NewTexture(
      escher()->NewCheckerboardImage(14, 4), vk::Filter::eNearest);
  checkerboard_material_ = fxl::MakeRefCounted<escher::Material>();
  checkerboard_material_->SetTexture(checkerboard);

  checkerboard_material_->set_color(
      escher::SrgbToLinear(vec3(.164f, .254f, 0.278f)));

  // Create meshes for fancy wobble effect.
  MeshSpec spec{MeshAttribute::kPosition2D | MeshAttribute::kPositionOffset |
                MeshAttribute::kPerimeterPos | MeshAttribute::kUV};
  ring_mesh1_ = escher::NewRingMesh(escher(), spec, 8, vec2(0.f, 0.f), 300.f,
                                    250.f, 18.f, -15.f);
  ring_mesh2_ = escher::NewRingMesh(escher(), spec, 8, vec2(0.f, 0.f), 200.f,
                                    150.f, 11.f, -8.f);
  ring_mesh3_ = escher::NewRingMesh(escher(), spec, 8, vec2(0.f, 0.f), 100.f,
                                    50.f, 5.f, -2.f);

  // Make this mesh the size of the stage
  float screenWidth = stage->viewing_volume().width();
  float screenHeight = stage->viewing_volume().height();

  // Make this mesh the size of the stage
  wobbly_ocean_mesh_ = escher::NewRectangleMesh(
      escher(), spec, 8, vec2(screenWidth, screenHeight * 0.5f), vec2(0.f, 0.f),
      18.f, 0.f);
}

WobblyOceanScene::~WobblyOceanScene() {}

escher::Model* WobblyOceanScene::Update(
    const escher::Stopwatch& stopwatch, uint64_t frame_count,
    escher::Stage* stage, escher::PaperRenderQueue* render_queue) {
  float current_time_sec = stopwatch.GetElapsedSeconds();

  float screenWidth = stage->viewing_volume().width();
  float screenHeight = stage->viewing_volume().height();
  float minElevation = stage->viewing_volume().bottom();
  float maxElevation = stage->viewing_volume().top();
  float elevationRange = maxElevation - minElevation;

  std::vector<Object> objects;

  vec3 ring_pos(screenWidth * 0.5, screenHeight * 0.5, 10.f);
  Object ring1(ring_pos + vec3(0, 0, 4.f), ring_mesh1_, color4_);
  ring1.set_shape_modifiers(ShapeModifier::kWobble);
  Object ring2(ring_pos + vec3(75., 0, 12.f), ring_mesh2_, color1_);
  ring2.set_shape_modifiers(ShapeModifier::kWobble);
  Object ring3(ring_pos + vec3(-125.0, 0, 24.f), ring_mesh3_, color3_);
  ring3.set_shape_modifiers(ShapeModifier::kWobble);

  constexpr float TWO_PI = 6.28318530718f;
  escher::ModifierWobble wobble_data1{{{-0.3f * TWO_PI, 0.4f, 7.f * TWO_PI},
                                       {-0.15 * TWO_PI, 0.2, 14.f * TWO_PI},
                                       {0 * TWO_PI, 0, 0 * TWO_PI}}};
  escher::ModifierWobble wobble_data2{{{0.3f * TWO_PI, 0.5f, 10.f * TWO_PI},
                                       {0.15f * TWO_PI, 0.3f, 15.f * TWO_PI},
                                       {0.2f * TWO_PI, 0.2f, 18.f * TWO_PI}}};
  escher::ModifierWobble wobble_data3{{{-0.6f * TWO_PI, 1.2f, 12.f * TWO_PI},
                                       {-.3f * TWO_PI, 0.8f, 8.f * TWO_PI},
                                       {0.4 * TWO_PI, 0.5, 15.f * TWO_PI}}};
  ring1.set_shape_modifier_data(wobble_data1);
  ring2.set_shape_modifier_data(wobble_data2);
  ring3.set_shape_modifier_data(wobble_data3);

  objects.push_back(ring1);
  objects.push_back(ring2);
  objects.push_back(ring3);

  escher::ModifierWobble ocean_wobble_data{
      {{-0.1f * TWO_PI, 0.75f, 7.f * TWO_PI},
       {-.2f * TWO_PI, .3f, 12.f * TWO_PI},
       {-.5f * TWO_PI, .1f, 16.f * TWO_PI}}};

  // Create a wobbly rectangle
  Object ocean_rect1(vec3(0.f, screenHeight * 0.65f, 2.f), wobbly_ocean_mesh_,
                     checkerboard_material_);
  ocean_rect1.set_shape_modifiers(ShapeModifier::kWobble);
  ocean_rect1.set_shape_modifier_data(ocean_wobble_data);
  objects.push_back(ocean_rect1);

  // Orbiting circle1
  float circle1_orbit_radius = 275.f;
  float circle1_x_pos = sin(current_time_sec * 0.85f) * circle1_orbit_radius +
                        (screenWidth * 0.65f);
  float circle1_y_pos = cos(current_time_sec * 0.85f) * circle1_orbit_radius +
                        (screenHeight * 0.35f);
  float circle1_elevation =
      (sin(current_time_sec * 0.85f + 0.5f) * 0.5f + 0.5f) * elevationRange +
      minElevation;
  Object circle1(Object::NewCircle(
      vec3(circle1_x_pos, circle1_y_pos, circle1_elevation), 60.f, color2_));
  objects.push_back(circle1);

  // Create our background plane
  Object bg_plane(Object::NewRect(vec2(0.f, 0.f),
                                  vec2(screenWidth, screenHeight), 0.f, bg_));

  objects.push_back(bg_plane);

  // Create the Model
  model_ = std::unique_ptr<escher::Model>(new escher::Model(objects));
  model_->set_time(current_time_sec);

  return model_.get();
}
