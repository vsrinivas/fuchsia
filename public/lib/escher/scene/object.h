// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_SCENE_OBJECT_H_
#define LIB_ESCHER_SCENE_OBJECT_H_

#include <unordered_map>

#include "lib/escher/geometry/transform.h"
#include "lib/escher/material/material.h"
#include "lib/escher/scene/shape.h"

namespace escher {

// An object instance to be drawn using a shape and a material.
// Does not retain ownership of the material.
class Object {
 public:
  ~Object();

  // Constructors.
  Object(const Transform& transform, MeshPtr mesh, MaterialPtr material);
  Object(const mat4& transform, MeshPtr mesh, MaterialPtr material);
  Object(const vec3& position, MeshPtr mesh, MaterialPtr material);
  Object(std::vector<Object> clippers, std::vector<Object> clippees);
  Object(const Object& other) = default;
  Object(Object&& other) = default;
  static Object NewRect(const vec2& top_left_position, const vec2& size,
                        float z, MaterialPtr material);
  static Object NewRect(const vec3& top_left_position, const vec2& size,
                        MaterialPtr material);
  static Object NewRect(const Transform& transform, MaterialPtr material);
  static Object NewRect(const mat4& transform, MaterialPtr material);
  static Object NewCircle(const vec2& center_position, float radius, float z,
                          MaterialPtr material);
  static Object NewCircle(const vec3& center_position, float radius,
                          MaterialPtr material);
  static Object NewCircle(const mat4& transform, float radius,
                          MaterialPtr material);

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

  Object& set_shape_modifiers(ShapeModifiers modifiers) {
    shape_.set_modifiers(modifiers);
    return *this;
  }

  // Obtain a temporary reference to data corresponding to a particular
  // ShapeModifier; the modifier that is used is determined by DataT::kType.
  // The returned pointer is invalidated by the next call to
  // set_shape_modifier_data().  Escher clients should only use pre-existing
  // Escher types for DataT (e.g. ModifierWobble), since those are the ones that
  // the renderer implementation knows how to deal with.
  template <typename DataT>
  const DataT* shape_modifier_data() const;
  // Set per-object ShapeModifier data for the ShapeModifier type specified by
  // DataT::kType.  Uses memcpy() to copy the DataT into shape_modifier_data_.
  template <typename DataT>
  void set_shape_modifier_data(const DataT& data);
  // Remove both the shape modifier data and the flag from the shape.
  template <typename DataT>
  void remove_shape_modifier();

  // Return the list of objects whose shapes will be used to clip 'clippees()'.
  // It is OK for these objects to not have a material; in this case the objects
  // update the stencil buffer, but not the color/depth buffers.
  const std::vector<Object>& clippers() const { return clippers_; }

  // Return the list of objects whose shapes will be clipped by 'clippers()'.
  const std::vector<Object>& clippees() const { return clippees_; }

 private:
  Object(mat4 transform, Shape shape, MaterialPtr material)
      : transform_(transform),
        shape_(std::move(shape)),
        material_(std::move(material)) {}

  mat4 transform_;
  Shape shape_;
  MaterialPtr material_;
  std::unordered_map<ShapeModifier, std::vector<uint8_t>> shape_modifier_data_;
  std::vector<Object> clippers_;
  std::vector<Object> clippees_;
};

// Inline function definitions.

template <typename DataT>
const DataT* Object::shape_modifier_data() const {
  auto it = shape_modifier_data_.find(DataT::kType);
  if (it == shape_modifier_data_.end()) {
    return nullptr;
  } else {
    FXL_DCHECK(it->second.size() == sizeof(DataT));
    return reinterpret_cast<const DataT*>(it->second.data());
  }
}

template <typename DataT>
void Object::set_shape_modifier_data(const DataT& data) {
  auto& vect = shape_modifier_data_[DataT::kType];
  vect.resize(sizeof(DataT));
  memcpy(vect.data(), &data, sizeof(DataT));
}

template <typename DataT>
void Object::remove_shape_modifier() {
  shape_modifier_data_.erase(DataT::kType);
  shape_.remove_modifier(DataT::kType);
}

template <typename DataT>
void set_shape_modifier_data(const DataT& data);

}  // namespace escher

#endif  // LIB_ESCHER_SCENE_OBJECT_H_
