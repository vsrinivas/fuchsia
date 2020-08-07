// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_SEMANTIC_TRANSFORM_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_SEMANTIC_TRANSFORM_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>

#include <array>

namespace a11y {

// A SemanticTransform represents a chain of local transformations from all of the
// nodes in a specific path from the root of the SemanticTree.
class SemanticTransform {
 public:
  // Takes a matrix from fuchsia.accessibility.semantics.Node's |transform| field
  // and logically prepends it to the list of transforms to apply (left-multiplying it with
  // the already applied transforms).
  void ChainLocalTransform(const fuchsia::ui::gfx::mat4& local_transform);

  // Transform the given point using the accumulated transforms.
  fuchsia::ui::gfx::vec3 Apply(const fuchsia::ui::gfx::vec3& point) const;

  // Return a vector with the resulting scale factors for each component
  const std::array<float, 3>& scale_vector() const { return scale_vector_; }

  // Return a vector with the resulting translation values for each component
  const std::array<float, 3>& translation_vector() const { return translation_vector_; }

 private:
  std::array<float, 3> scale_vector_ = {1.0f, 1.0f, 1.0f};
  std::array<float, 3> translation_vector_ = {0.f, 0.f, 0.f};
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_SEMANTIC_TRANSFORM_H_
