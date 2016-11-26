// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>
#include <glm/glm.hpp>

#include "escher/material/material.h"
#include "escher/scene/shape.h"

namespace escher {

// An object instance to be drawn using a shape and a material.
// Does not retain ownership of the material.
class Object {
 public:
  ~Object();

  // Constructors.
  Object(MeshPtr mesh,
         const vec3& position,
         MaterialPtr material,
         vec2 scale = vec2(1.f, 1.f));
  static Object NewRect(const vec2& position,
                        const vec2& size,
                        float z,
                        MaterialPtr material);
  static Object NewCircle(const vec2& center,
                          float radius,
                          float z,
                          MaterialPtr material);

  // The shape to draw.
  const Shape& shape() const { return shape_; }

  // The material with which to fill the shape.
  const MaterialPtr& material() const { return material_; }

  const vec3& position() const { return position_; }

  // For circles and rects: width and height of this object
  // For objects created from a Mesh: scale factor of this object in x/y
  // dimensions
  float width() const { return size_.x; }
  float height() const { return size_.y; }
  float rotation() const { return rotation_; }
  const vec2& rotation_point() const { return rotation_point_; }

  void set_rotation(float rotation) { rotation_ = rotation; }
  void set_rotation_point(const vec2& rotation_point) {
    rotation_point_ = rotation_point;
  }

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

 private:
  Object(const Shape& shape, const MaterialPtr& material);

  Shape shape_;
  MaterialPtr material_;
  vec3 position_;
  vec2 size_;
  float rotation_;
  vec2 rotation_point_;
  std::unordered_map<ShapeModifier, std::vector<uint8_t>> shape_modifier_data_;
};

// Inline function definitions.

template <typename DataT>
const DataT* Object::shape_modifier_data() const {
  auto it = shape_modifier_data_.find(DataT::kType);
  if (it == shape_modifier_data_.end()) {
    return nullptr;
  } else {
    FTL_DCHECK(it->second.size() == sizeof(DataT));
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
void set_shape_modifier_data(const DataT& data);

}  // namespace escher
