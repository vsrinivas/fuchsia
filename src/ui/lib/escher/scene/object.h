// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_SCENE_OBJECT_H_
#define SRC_UI_LIB_ESCHER_SCENE_OBJECT_H_

#include <unordered_map>

#include "src/ui/lib/escher/geometry/transform.h"
#include "src/ui/lib/escher/material/material.h"
#include "src/ui/lib/escher/scene/shape.h"

namespace escher {

// An object instance to be drawn using a shape and a material.
// Does not retain ownership of the material.
class Object {
 public:
  // Constructors.
  Object(const Transform& transform, MeshPtr mesh, MaterialPtr material);
  Object(const mat4& transform, MeshPtr mesh, MaterialPtr material);
  Object(const vec3& position, MeshPtr mesh, MaterialPtr material);
  Object(std::vector<Object> clippers, std::vector<Object> clippees);
  Object(const Object& other) = default;
  Object(Object&& other) = default;
  static Object NewRect(const vec2& top_left_position, const vec2& size, float z,
                        MaterialPtr material);
  static Object NewRect(const vec3& top_left_position, const vec2& size, MaterialPtr material);
  static Object NewRect(const Transform& transform, MaterialPtr material);
  static Object NewRect(const mat4& transform, MaterialPtr material);
  static Object NewCircle(const vec2& center_position, float radius, float z, MaterialPtr material);
  static Object NewCircle(const vec3& center_position, float radius, MaterialPtr material);
  static Object NewCircle(const mat4& transform, float radius, MaterialPtr material);

  // Return the object's 4x4 transformation matrix.
  const mat4& transform() const { return transform_; }

  // The shape to draw.
  const Shape& shape() const { return shape_; }
  Shape& mutable_shape() { return shape_; }

  // The material with which to fill the shape.
  const MaterialPtr& material() const { return material_; }
  void set_material(MaterialPtr material) { material_ = std::move(material); }

  // Return the bounding box that encompasses the object's shape, as well as
  // all of its clippers (but not clippees, since their clipped bounds are
  // by definition within the clippers' bounds).
  BoundingBox bounding_box() const;

  // Return the list of objects whose shapes will be used to clip 'clippees()'.
  // It is OK for these objects to not have a material; in this case the objects
  // update the stencil buffer, but not the color/depth buffers.
  const std::vector<Object>& clippers() const { return clippers_; }

  // Return the list of objects whose shapes will be clipped by 'clippers()'.
  const std::vector<Object>& clippees() const { return clippees_; }

 private:
  Object(mat4 transform, Shape shape, MaterialPtr material)
      : transform_(transform), shape_(std::move(shape)), material_(std::move(material)) {}

  mat4 transform_;
  Shape shape_;
  MaterialPtr material_;
  std::vector<Object> clippers_;
  std::vector<Object> clippees_;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_SCENE_OBJECT_H_
