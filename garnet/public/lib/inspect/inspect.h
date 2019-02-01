// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_INSPECT_H_
#define LIB_INSPECT_INSPECT_H_

#include <lib/component/cpp/exposed_object.h>
#include <zircon/types.h>

#include <string>

namespace inspect {

class Object;

namespace internal {

// Factories for creating metrics.
template <typename T>
component::Metric MakeMetric(T value);

// Factory to create an IntMetric.
template <>
component::Metric MakeMetric<int64_t>(int64_t value);

// Factory to create a UIntMetric.
template <>
component::Metric MakeMetric<uint64_t>(uint64_t value);

// Factory to create a DoubleMetric.
template <>
component::Metric MakeMetric<double>(double value);

// Template for functions that remove a named entity of a given type from an
// object.
template <typename EntityType>
void RemoveEntity(::component::Object* object, const std::string& name);

// Removal function for Metric.
template <>
void RemoveEntity<component::Metric>(::component::Object* object,
                                     const std::string& name);

// Removal function for Property.
template <>
void RemoveEntity<component::Property>(::component::Object* object,
                                       const std::string& name);

// Wrapper class for entity types supported by the Inspect API.
// This class implements RAII behavior for created Inspect entities,
// including removing values from the tree when the owner goes out of scope.
template <typename EntityType>
class EntityWrapper final {
 public:
  EntityWrapper() {}
  explicit EntityWrapper(std::string name) : name_(std::move(name)) {}
  EntityWrapper(std::string name, fbl::RefPtr<::component::Object> obj)
      : name_(std::move(name)), parent_obj_(std::move(obj)) {}

  ~EntityWrapper() {
    // Remove the entity from its parent if it has a parent.
    if (parent_obj_) {
      RemoveEntity<EntityType>(parent_obj_.get(), name_);
    }
  }

  // Allow moving, disallow copying.
  EntityWrapper(EntityWrapper& other) = delete;
  EntityWrapper& operator=(EntityWrapper& other) = delete;
  EntityWrapper(EntityWrapper&& other) = default;
  EntityWrapper& operator=(EntityWrapper&& other) {
    // Remove the entity from its parent before moving values over.
    if (parent_obj_) {
      RemoveEntity<EntityType>(parent_obj_.get(), name_);
    }
    name_ = std::move(other.name_);
    parent_obj_ = std::move(other.parent_obj_);
    return *this;
  }

  explicit operator bool() const { return parent_obj_.get() != nullptr; }

  // Get the name for this entity.
  const std::string& name() const { return name_; }

  ::component::Object* ParentObject() { return parent_obj_.get(); }

  std::string name_;
  fbl::RefPtr<::component::Object> parent_obj_;
};

}  // namespace internal

// Template for metrics that are concretely stored in memeory (as opposed to
// CallbackMetrics, which are dynamically created when needed). StaticMetrics
// can be set, added to, and subtracted from.
template <typename T>
class StaticMetric final {
 public:
  // Create a default numeric metric.
  // Operations on the metric will have no effect.
  StaticMetric() = default;

  // Set the value of this numeric metric to the given value.
  void Set(T value) {
    if (entity_) {
      entity_.ParentObject()->SetMetric(entity_.name(),
                                        internal::MakeMetric<T>(value));
    }
  }

  // Add the given value to the value of this numeric metric.
  void Add(T value) {
    if (entity_) {
      entity_.ParentObject()->AddMetric(entity_.name(), value);
    }
  }

  // Subtract the given value from the value of this numeric metric.
  void Subtract(T value) {
    if (entity_) {
      entity_.ParentObject()->SubMetric(entity_.name(), value);
    }
  }

 private:
  friend class ::inspect::Object;
  // Internal constructor setting an actual value on an Object.
  explicit StaticMetric(internal::EntityWrapper<component::Metric> entity)
      : entity_(std::move(entity)) {}

  internal::EntityWrapper<component::Metric> entity_;
};

// Metric wrapping a signed integer.
using IntMetric = StaticMetric<int64_t>;

// Metric wrapped an unsigned integer.
using UIntMetric = StaticMetric<uint64_t>;

// Metric wrapping a double floating point number.
using DoubleMetric = StaticMetric<double>;

// Metric with value determined by evaluating a callback.
class CallbackMetric final {
 public:
  // Construct a default metric.
  // Operations on the metric will have no effect.
  CallbackMetric();

  // Set the callback used to return the value of the metric.
  void Set(::component::Metric::ValueCallback callback);

 private:
  friend class ::inspect::Object;
  // Internal constructor setting an actual value on an Object.
  explicit CallbackMetric(internal::EntityWrapper<component::Metric> entity);

  internal::EntityWrapper<component::Metric> entity_;
};

// The value of a metric, currently an alias for component::Metric.
using MetricValue = ::component::Metric;

using MetricCallback = ::component::Metric::ValueCallback;

// Property with value given by a string.
class StringProperty final {
 public:
  // Construct a default property.
  // Operations on the property will have no effect.
  StringProperty();

  // Set the string value of the property.
  void Set(std::string value);

 private:
  friend class ::inspect::Object;
  // Internal constructor setting an actual value on an Object.
  explicit StringProperty(internal::EntityWrapper<component::Property> entity);

  internal::EntityWrapper<component::Property> entity_;
};

// Property with value given by an array of bytes.
class ByteVectorProperty final {
 public:
  // Construct a default property.
  // Operations on the property will have no effect.
  ByteVectorProperty();

  // Set the vector value of the property.
  void Set(::component::Property::ByteVector value);

 private:
  friend class ::inspect::Object;
  // Internal constructor setting an actual value on an Object.
  explicit ByteVectorProperty(
      internal::EntityWrapper<component::Property> entity);

  internal::EntityWrapper<component::Property> entity_;
};

// Callback type for string values.
using StringValueCallback = ::component::Property::StringValueCallback;

// Property with string value determined by evaluating a callback.
class LazyStringProperty final {
 public:
  // Construct a default property.
  // Operations on the property will have no effect.
  LazyStringProperty();

  // Set the callback that generates the value of the property.
  void Set(StringValueCallback callback);

 private:
  friend class ::inspect::Object;
  // Internal constructor setting an actual value on an Object.
  explicit LazyStringProperty(
      internal::EntityWrapper<component::Property> entity);

  internal::EntityWrapper<component::Property> entity_;
};

// Callback type for vector values.
using VectorValueCallback = ::component::Property::VectorValueCallback;

// Property with byte vector value determined by evaluating a callback.
class LazyByteVectorProperty final {
 public:
  // Construct a default property.
  // Operations on the property will have no effect.
  LazyByteVectorProperty();

  // Set the callback that generates the value of the property.
  void Set(VectorValueCallback callback);

 private:
  friend class ::inspect::Object;
  // Internal constructor setting an actual value on an Object.
  explicit LazyByteVectorProperty(
      internal::EntityWrapper<component::Property> entity);

  internal::EntityWrapper<component::Property> entity_;
};

// Value of vector types, currently an alias for
// component::Property::ByteVector.
using VectorValue = component::Property::ByteVector;

using ChildrenCallbackFunction = ::component::Object::ChildrenCallback;

// ChildrenCallback is an RAII wrapper around a callback attached to an
// Object that provides additional children dynamically.
class ChildrenCallback final {
 public:
  // Construct a default children callback.
  ChildrenCallback();
  ~ChildrenCallback();

  // Set the callback function for the parent object to the given value.
  void Set(ChildrenCallbackFunction callback);

  // Allow moving, disallow copying.
  ChildrenCallback(ChildrenCallback& other) = delete;
  ChildrenCallback& operator=(ChildrenCallback& other) = delete;
  ChildrenCallback(ChildrenCallback&& other) = default;
  ChildrenCallback& operator=(ChildrenCallback&& other);

 private:
  friend class ::inspect::Object;
  // Internal constructor setting an actual children callback on an object.
  ChildrenCallback(fbl::RefPtr<::component::Object> object);

  fbl::RefPtr<::component::Object> parent_obj_;
};

// An object under which properties, metrics, and other objects may be nested.
class Object final {
 public:
  // Construct an Object with the given name.
  explicit Object(std::string name);

  // Construct an Object wrapping the given ObjectDir.
  explicit Object(component::ObjectDir object_dir);

  ~Object() = default;

  // Get the name of this object.
  const char* name() const { return object_.object()->name().c_str(); }

  // Output the contents of this object as a FIDL struct.
  fuchsia::inspect::Object object() const;

  // Output the list of this object's children as a FIDL-compatible vector.
  ::component::Object::StringOutputVector children() const;

  // Allow moving, disallow copying.
  Object(Object& other) = delete;
  Object& operator=(Object& other) = delete;
  Object(Object&& other) = default;
  Object& operator=(Object&& other) = default;

  // Create a new |Object| with the given name that is a child of this object.
  [[nodiscard]] Object CreateChild(std::string name);

  // Create a new |IntMetric| with the given name that is a child of this
  // object.
  [[nodiscard]] IntMetric CreateIntMetric(std::string name, int64_t value);

  // Create a new |UIntMetric| with the given name that is a child of this
  // object.
  [[nodiscard]] UIntMetric CreateUIntMetric(std::string name, uint64_t value);

  // Create a new |DoubleMetric| with the given name that is a child of this
  // object.
  [[nodiscard]] DoubleMetric CreateDoubleMetric(std::string name, double value);

  // Create a new |StringProperty| with the given name that is a child of this
  // object.
  [[nodiscard]] StringProperty CreateStringProperty(std::string name,
                                                    std::string value);

  // Create a new |ByteVectorProperty| with the given name that is a child of
  // this object.
  [[nodiscard]] ByteVectorProperty CreateByteVectorProperty(
      std::string name, component::Property::ByteVector value);

  // Create a new |StringCallbackProperty| with the given name that is a child
  // of this object.
  [[nodiscard]] LazyStringProperty CreateLazyStringProperty(
      std::string name, component::Property::StringValueCallback callback);

  // Create a new |VectorCallbackProperty| with the given name that is a child
  // of this object.
  [[nodiscard]] LazyByteVectorProperty CreateLazyByteVectorProperty(
      std::string name, component::Property::VectorValueCallback callback);

  // Create a new |CallbackMetric| with the given name that is a child of this
  // object.
  [[nodiscard]] CallbackMetric CreateCallbackMetric(
      std::string name, component::Metric::ValueCallback);

  // Create a new |ChildrenCallback| that dynamically adds children to the
  // object at runtime.
  [[nodiscard]] ChildrenCallback CreateChildrenCallback(
      ChildrenCallbackFunction callback);

 private:
  // Internal constructor for creating an Object facade in front of an
  // ExposedObject.
  explicit Object(component::ExposedObject object)
      : object_(std::move(object)) {}

  component::ExposedObject object_;
};

// Generate a unique name with the given prefix.
std::string UniqueName(const std::string& prefix);

}  // namespace inspect

#endif  // LIB_INSPECT_INSPECT_H_
