// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/inspect/inspect.h"

using component::ObjectDir;

namespace inspect {

template <>
component::Metric internal::MakeMetric<int64_t>(int64_t value) {
  return component::IntMetric(value);
}

template <>
component::Metric internal::MakeMetric<uint64_t>(uint64_t value) {
  return component::UIntMetric(value);
}

template <>
component::Metric internal::MakeMetric<double>(double value) {
  return component::DoubleMetric(value);
}

template <>
void internal::RemoveEntity<component::Property>(component::Object* object,
                                                 const std::string& name) {
  object->RemoveProperty(name);
}

template <>
void internal::RemoveEntity<component::Metric>(component::Object* object,
                                               const std::string& name) {
  object->RemoveMetric(name);
}

LazyMetric::LazyMetric() {}

LazyMetric::LazyMetric(internal::EntityWrapper<component::Metric> entity)
    : entity_(std::move(entity)) {}
void LazyMetric::Set(MetricCallback callback) {
  if (entity_) {
    entity_.ParentObject()->SetMetric(
        entity_.name(), component::CallbackMetric(std::move(callback)));
  }
}

#define DEFINE_PROPERTY_METHODS(CLASS, TYPE)                        \
  CLASS::CLASS() {}                                                 \
  CLASS::CLASS(internal::EntityWrapper<component::Property> entity) \
      : entity_(std::move(entity)) {}                               \
  void CLASS::Set(TYPE value) {                                     \
    if (entity_) {                                                  \
      entity_.ParentObject()->SetProperty(                          \
          entity_.name(), component::Property(std::move(value)));   \
    }                                                               \
  }

DEFINE_PROPERTY_METHODS(StringProperty, std::string)
DEFINE_PROPERTY_METHODS(ByteVectorProperty, VectorValue)
DEFINE_PROPERTY_METHODS(LazyStringProperty, StringValueCallback)
DEFINE_PROPERTY_METHODS(LazyByteVectorProperty, VectorValueCallback)

ChildrenCallback::ChildrenCallback() {}

ChildrenCallback::ChildrenCallback(std::shared_ptr<component::Object> object)
    : parent_obj_(std::move(object)) {}

ChildrenCallback::~ChildrenCallback() {
  // Remove the entity from its parent if it has a parent.
  if (parent_obj_) {
    parent_obj_->ClearChildrenCallback();
  }
}

void ChildrenCallback::Set(ChildrenCallbackFunction callback) {
  if (parent_obj_) {
    parent_obj_->SetChildrenCallback(std::move(callback));
  }
}

ChildrenCallback& ChildrenCallback::operator=(ChildrenCallback&& other) {
  // Remove the entity from its parent before moving values over.
  if (parent_obj_ && parent_obj_.get() != other.parent_obj_.get()) {
    parent_obj_->ClearChildrenCallback();
  }
  parent_obj_ = std::move(other.parent_obj_);
  return *this;
}

Object::Object(std::string name)
    : object_(std::make_unique<component::ExposedObject>(std::move(name))) {}

Object::Object(ObjectDir object_dir)
    : object_(
          std::make_unique<component::ExposedObject>(std::move(object_dir))) {}

fuchsia::inspect::Object Object::object() const {
  return object_ ? object_->object()->ToFidl() : fuchsia::inspect::Object();
}

component::Object::StringOutputVector Object::children() const {
  return object_ ? object_->object()->GetChildren()
                 : component::Object::StringOutputVector();
}

Object Object::CreateChild(std::string name) {
  if (!object_) {
    return Object();
  }
  component::ExposedObject child(std::move(name));
  object_->add_child(&child);
  return Object(std::move(child));
}

IntMetric Object::CreateIntMetric(std::string name, int64_t value) {
  if (!object_) {
    return IntMetric();
  }
  object_->object()->SetMetric(name, component::IntMetric(value));
  return IntMetric(internal::EntityWrapper<component::Metric>(
      std::move(name), object_->object()));
}

UIntMetric Object::CreateUIntMetric(std::string name, uint64_t value) {
  if (!object_) {
    return UIntMetric();
  }
  object_->object()->SetMetric(name, component::UIntMetric(value));
  return UIntMetric(internal::EntityWrapper<component::Metric>(
      std::move(name), object_->object()));
}

DoubleMetric Object::CreateDoubleMetric(std::string name, double value) {
  if (!object_) {
    return DoubleMetric();
  }
  object_->object()->SetMetric(name, component::DoubleMetric(value));
  return DoubleMetric(internal::EntityWrapper<component::Metric>(
      std::move(name), object_->object()));
}

LazyMetric Object::CreateLazyMetric(std::string name,
                                    component::Metric::ValueCallback callback) {
  if (!object_) {
    return LazyMetric();
  }
  object_->object()->SetMetric(name,
                               component::CallbackMetric(std::move(callback)));
  return LazyMetric(internal::EntityWrapper<component::Metric>(
      std::move(name), object_->object()));
}

StringProperty Object::CreateStringProperty(std::string name,
                                            std::string value) {
  if (!object_) {
    return StringProperty();
  }
  object_->object()->SetProperty(name, component::Property(std::move(value)));
  return StringProperty(internal::EntityWrapper<component::Property>(
      std::move(name), object_->object()));
}

ByteVectorProperty Object::CreateByteVectorProperty(std::string name,
                                                    VectorValue value) {
  if (!object_) {
    return ByteVectorProperty();
  }
  object_->object()->SetProperty(name, component::Property(std::move(value)));
  return ByteVectorProperty(internal::EntityWrapper<component::Property>(
      std::move(name), object_->object()));
}

LazyStringProperty Object::CreateLazyStringProperty(std::string name,
                                                    StringValueCallback value) {
  if (!object_) {
    return LazyStringProperty();
  }
  object_->object()->SetProperty(name, component::Property(std::move(value)));
  return LazyStringProperty(internal::EntityWrapper<component::Property>(
      std::move(name), object_->object()));
}

LazyByteVectorProperty Object::CreateLazyByteVectorProperty(
    std::string name, VectorValueCallback value) {
  if (!object_) {
    return LazyByteVectorProperty();
  }
  object_->object()->SetProperty(name, component::Property(std::move(value)));
  return LazyByteVectorProperty(internal::EntityWrapper<component::Property>(
      std::move(name), object_->object()));
}

ChildrenCallback Object::CreateChildrenCallback(
    ChildrenCallbackFunction callback) {
  if (!object_) {
    return ChildrenCallback();
  }
  object_->object()->SetChildrenCallback(std::move(callback));
  return ChildrenCallback(object_->object());
}

std::string UniqueName(const std::string& prefix) {
  return component::ExposedObject::UniqueName(prefix);
}

}  // namespace inspect
