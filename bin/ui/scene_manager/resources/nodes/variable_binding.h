// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_NODES_VARIABLE_BINDING_H_
#define GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_NODES_VARIABLE_BINDING_H_

#include "garnet/bin/ui/scene_manager/resources/variable.h"

namespace scene_manager {

class Node;

// Binds a Variable to a particular callback function. Observes when the
// Variable's value changes and invokes the callback. The act of creating the
// binding automatically sets the value of the target.
class VariableBinding {
 public:
  virtual ~VariableBinding(){};
};

template <scenic::Value::Tag VT, typename T>
class TypedVariableBinding : public VariableBinding,
                             public OnVariableValueChangedListener<VT, T> {
 public:
  TypedVariableBinding(fxl::RefPtr<TypedVariable<VT, T>> variable,
                       std::function<void(T value)> on_value_changed_callback);
  virtual ~TypedVariableBinding();

 private:
  void OnVariableValueChanged(TypedVariable<VT, T>* v) override;

  fxl::RefPtr<TypedVariable<VT, T>> variable_;
  std::function<void(T value)> on_value_changed_callback_;
};

using Vector3VariableBinding =
    TypedVariableBinding<scenic::Value::Tag::VECTOR3, escher::vec3>;
using QuaternionVariableBinding =
    TypedVariableBinding<scenic::Value::Tag::QUATERNION, escher::quat>;

}  // namespace scene_manager

#endif  // GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_NODES_VARIABLE_BINDING_H_
