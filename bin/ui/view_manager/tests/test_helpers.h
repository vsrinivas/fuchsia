// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_TESTS_TEST_HELPERS_H_
#define GARNET_BIN_UI_VIEW_MANAGER_TESTS_TEST_HELPERS_H_

#include <utility>

#include "lib/ui/views/fidl/view_associates.fidl.h"
#include "lib/fxl/functional/closure.h"

namespace test {

template <typename T>
void Capture(const fxl::Closure& quit, T* out, T value) {
  *out = std::move(value);
  quit();
}

inline mozart::PointFPtr MakePointF(float x, float y) {
  auto result = mozart::PointF::New();
  result->x = x;
  result->y = y;
  return result.Pass();
}

inline mozart::TransformPtr MakeDummyTransform(float x) {
  auto result = mozart::Transform::New();
  result->matrix.resize(16u);
  result->matrix[0] = x;
  return result.Pass();
}

inline mozart::SceneTokenPtr MakeDummySceneToken(uint32_t value) {
  auto result = mozart::SceneToken::New();
  result->value = value;
  return result.Pass();
}

inline mozart::ViewTokenPtr MakeDummyViewToken(uint32_t value) {
  auto result = mozart::ViewToken::New();
  result->value = value;
  return result.Pass();
}

inline mozart::HitTestResultPtr MakeSimpleHitTestResult(
    mozart::SceneTokenPtr scene_token,
    mozart::TransformPtr transform) {
  auto result = mozart::HitTestResult::New();
  result->root = mozart::SceneHit::New();
  result->root->scene_token = scene_token.Pass();
  result->root->hits.push_back(mozart::Hit::New());
  result->root->hits[0]->set_node(mozart::NodeHit::New());
  result->root->hits[0]->get_node()->transform = transform.Pass();
  return result.Pass();
}

inline mozart::HitTestResultPtr MakeSimpleHitTestResult(
    mozart::SceneTokenPtr scene_token) {
  return MakeSimpleHitTestResult(scene_token.Pass(), MakeDummyTransform(0.f));
}

}  // namespace test

#endif  // GARNET_BIN_UI_VIEW_MANAGER_TESTS_TEST_HELPERS_H_
