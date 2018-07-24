// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_TESTS_TEST_HELPERS_H_
#define GARNET_BIN_UI_VIEW_MANAGER_TESTS_TEST_HELPERS_H_

#include <utility>

#include <lib/fit/function.h>

namespace test {

template <typename T>
void Capture(fit::closure quit, T* out, T value) {
  *out = std::move(value);
  quit();
}

inline fuchsia::math::PointFPtr MakePointF(float x, float y) {
  auto result = fuchsia::math::PointF::New();
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

inline ::fuchsia::ui::viewsv1token::ViewTokenPtr MakeDummyViewToken(uint32_t value) {
  auto result = ::fuchsia::ui::viewsv1token::ViewToken::New();
  result->value = value;
  return result.Pass();
}

inline mozart::HitTestResultPtr MakeSimpleHitTestResult(
    mozart::SceneTokenPtr scene_token, mozart::TransformPtr transform) {
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
