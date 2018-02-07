// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/resources/variable.h"

#include "garnet/lib/ui/scenic/util/error_reporter.h"
#include "garnet/lib/ui/scenic/util/unwrap.h"

namespace scene_manager {

const ResourceTypeInfo Variable::kTypeInfo = {ResourceType::kVariable,
                                              "Variable"};

Variable::Variable(Session* session, scenic::ResourceId id)
    : Resource(session, id, Variable::kTypeInfo) {}

Variable::~Variable() {}

template <scenic::Value::Tag VT, typename T>
TypedVariable<VT, T>::TypedVariable(Session* session, scenic::ResourceId id)
    : Variable(session, id),
      value_()  // Initialize |value_| to its type's default value.
{}

template <scenic::Value::Tag VT, typename T>
void TypedVariable<VT, T>::SetValue(T value) {
  value_ = value;
  for (auto listener : listeners_)
    listener->OnVariableValueChanged(this);
}

template <scenic::Value::Tag VT, typename T>
bool TypedVariable<VT, T>::SetValue(const scenic::ValuePtr& value) {
  bool success = Unwrap(value, &value_);
  FXL_DCHECK(success);
  for (auto listener : listeners_)
    listener->OnVariableValueChanged(this);
  return success;
}

template <scenic::Value::Tag VT, typename T>
void TypedVariable<VT, T>::Accept(class ResourceVisitor* visitor){};

// Explicitly instantiate all the classes in Scenic that use the Variable<>
// template.
template class TypedVariable<scenic::Value::Tag::VECTOR1, float>;
template class TypedVariable<scenic::Value::Tag::VECTOR2, escher::vec2>;
template class TypedVariable<scenic::Value::Tag::VECTOR3, escher::vec3>;
template class TypedVariable<scenic::Value::Tag::VECTOR4, escher::vec4>;
template class TypedVariable<scenic::Value::Tag::MATRIX4X4, escher::mat4>;
template class TypedVariable<scenic::Value::Tag::QUATERNION, escher::quat>;
// template class TypedVariable<scenic::Value::Tag::TRANSFORM,
// escher::Transform>;

}  // namespace scene_manager
