// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_VARIABLE_H_
#define GARNET_LIB_UI_GFX_RESOURCES_VARIABLE_H_

#include <set>

#include "garnet/lib/ui/gfx/resources/resource.h"

#include "garnet/lib/ui/scenic/util/error_reporter.h"
#include "lib/escher/geometry/transform.h"
#include "lib/escher/geometry/types.h"

namespace scenic {
namespace gfx {

class Variable;
template <::fuchsia::ui::gfx::Value::Tag VT, typename T>
class TypedVariable;

// Callback for watchers of Variable. Any class implementing this interface must
// hold a strong reference to Variable.
template <::fuchsia::ui::gfx::Value::Tag VT, typename T>
class OnVariableValueChangedListener {
 public:
  virtual void OnVariableValueChanged(TypedVariable<VT, T>* v) = 0;
};

class Variable : public Resource {
 public:
  Variable(Session* session, scenic::ResourceId id);
  virtual ~Variable();

  static const ResourceTypeInfo kTypeInfo;
  virtual ::fuchsia::ui::gfx::Value::Tag value_type() = 0;

  virtual bool SetValue(const ::fuchsia::ui::gfx::Value& value) = 0;
};

// Represents a variable whose value can change, usually via animations.
template <::fuchsia::ui::gfx::Value::Tag VT, typename T>
class TypedVariable : public Variable {
 public:
  TypedVariable(Session* session, scenic::ResourceId id);

  static constexpr ::fuchsia::ui::gfx::Value::Tag ValueType() { return VT; }
  ::fuchsia::ui::gfx::Value::Tag value_type() override { return VT; }

  T value() { return value_; }
  void SetValue(T value);
  bool SetValue(const ::fuchsia::ui::gfx::Value& value) override;

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

using FloatVariable =
    TypedVariable<::fuchsia::ui::gfx::Value::Tag::kVector1, float>;
using Vector2Variable =
    TypedVariable<::fuchsia::ui::gfx::Value::Tag::kVector2, escher::vec2>;
using Vector3Variable =
    TypedVariable<::fuchsia::ui::gfx::Value::Tag::kVector3, escher::vec3>;
using Vector4Variable =
    TypedVariable<::fuchsia::ui::gfx::Value::Tag::kVector4, escher::vec4>;
using Matrix4x4Variable =
    TypedVariable<::fuchsia::ui::gfx::Value::Tag::kMatrix4x4, escher::mat4>;
using QuaternionVariable =
    TypedVariable<::fuchsia::ui::gfx::Value::Tag::kQuaternion, escher::quat>;
// using TransformVariable =
//    TypedVariable<::fuchsia::ui::gfx::Value::Tag::kTransform,
//    escher::Transform>;

using FloatVariablePtr = fxl::RefPtr<FloatVariable>;
using Vector2VariablePtr = fxl::RefPtr<Vector2Variable>;
using Vector3VariablePtr = fxl::RefPtr<Vector3Variable>;
using Vector4VariablePtr = fxl::RefPtr<Vector4Variable>;
using Matrix4x4VariablePtr = fxl::RefPtr<Matrix4x4Variable>;
using QuaternionVariablePtr = fxl::RefPtr<QuaternionVariable>;
// using TransformVariablePtr = fxl::RefPtr<TransformVariable>;

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_RESOURCES_VARIABLE_H_
