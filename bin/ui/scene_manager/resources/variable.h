// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_VARIABLE_H_
#define GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_VARIABLE_H_

#include "garnet/bin/ui/scene_manager/resources/resource.h"

#include "garnet/bin/ui/scene_manager/util/error_reporter.h"
#include "lib/escher/geometry/transform.h"
#include "lib/escher/geometry/types.h"

namespace scene_manager {

class Variable;
template <scenic::Value::Tag VT, typename T>
class TypedVariable;

// Callback for watchers of Variable. Any class implementing this interface must
// hold a strong reference to Variable.
template <scenic::Value::Tag VT, typename T>
class OnVariableValueChangedListener {
 public:
  virtual void OnVariableValueChanged(TypedVariable<VT, T>* v) = 0;
};

class Variable : public Resource {
 public:
  Variable(Session* session, scenic::ResourceId id);
  virtual ~Variable();

  static const ResourceTypeInfo kTypeInfo;
  virtual scenic::Value::Tag value_type() = 0;

  virtual bool SetValue(const scenic::ValuePtr& value) = 0;
};

// Represents a variable whose value can change, usually via animations.
template <scenic::Value::Tag VT, typename T>
class TypedVariable : public Variable {
 public:
  TypedVariable(Session* session, scenic::ResourceId id);

  static constexpr scenic::Value::Tag ValueType() { return VT; }
  scenic::Value::Tag value_type() override { return VT; }

  T value() { return value_; }
  void SetValue(T value);
  bool SetValue(const scenic::ValuePtr& value) override;

  void AddListener(OnVariableValueChangedListener<VT, T>* listener) {
    listeners_.insert(listener);
  }

  void RemoveListener(OnVariableValueChangedListener<VT, T>* listener) {
    auto it = listeners_.find(listener);
    FXL_DCHECK(it != listeners_.end());
    listeners_.erase(it);
  }

  void Accept(class ResourceVisitor* visitor) override;

 private:
  void OnValueChanged(T new_value) { SetValue(new_value); }
  T value_;
  std::set<OnVariableValueChangedListener<VT, T>*> listeners_;
};

using FloatVariable = TypedVariable<scenic::Value::Tag::VECTOR1, float>;
using Vector2Variable =
    TypedVariable<scenic::Value::Tag::VECTOR2, escher::vec2>;
using Vector3Variable =
    TypedVariable<scenic::Value::Tag::VECTOR3, escher::vec3>;
using Vector4Variable =
    TypedVariable<scenic::Value::Tag::VECTOR4, escher::vec4>;
using Matrix4x4Variable =
    TypedVariable<scenic::Value::Tag::MATRIX4X4, escher::mat4>;
using QuaternionVariable =
    TypedVariable<scenic::Value::Tag::QUATERNION, escher::quat>;
// using TransformVariable =
//    TypedVariable<scenic::Value::Tag::TRANSFORM, escher::Transform>;

using FloatVariablePtr = fxl::RefPtr<FloatVariable>;
using Vector2VariablePtr = fxl::RefPtr<Vector2Variable>;
using Vector3VariablePtr = fxl::RefPtr<Vector3Variable>;
using Vector4VariablePtr = fxl::RefPtr<Vector4Variable>;
using Matrix4x4VariablePtr = fxl::RefPtr<Matrix4x4Variable>;
using QuaternionVariablePtr = fxl::RefPtr<QuaternionVariable>;
// using TransformVariablePtr = fxl::RefPtr<TransformVariable>;

}  // namespace scene_manager

#endif  // GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_VARIABLE_H_
