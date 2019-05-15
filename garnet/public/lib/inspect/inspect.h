// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_INSPECT_H_
#define LIB_INSPECT_INSPECT_H_

#include <lib/fit/variant.h>
#include <lib/inspect-vmo/inspect.h>
#include <lib/inspect/deprecated/exposed_object.h>
#include <zircon/types.h>

#include <string>

#include "lib/inspect-vmo/block.h"
#include "lib/inspect-vmo/types.h"

namespace inspect {

class Node;

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
  EntityWrapper(std::string name, std::shared_ptr<::component::Object> obj)
      : name_(std::move(name)), parent_obj_(std::move(obj)) {}

  ~EntityWrapper() {
    // Remove the entity from its parent if it has a parent.
    if (parent_obj_) {
      RemoveEntity<EntityType>(parent_obj_.get(), name_);
    }
  }

  // Allow moving, disallow copying.
  EntityWrapper(const EntityWrapper& other) = delete;
  EntityWrapper(EntityWrapper&& other) = default;
  EntityWrapper& operator=(const EntityWrapper& other) = delete;
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

 private:
  explicit EntityWrapper(std::string name) : name_(std::move(name)) {}

  std::string name_;
  std::shared_ptr<::component::Object> parent_obj_;
};

// Structure containing internal data for a |Tree|.
struct TreeState;

}  // namespace internal

// Template for metrics that are concretely stored in memory (as opposed to
// LazyMetrics, which are dynamically created when needed). StaticMetrics
// can be set, added to, and subtracted from.
template <typename T, typename VmoType>
class StaticMetric final {
 public:
  // Create a default numeric metric.
  // Operations on the metric will have no effect.
  StaticMetric() = default;

  // Set the value of this numeric metric to the given value.
  void Set(T value) {
    if (entity_.index() == kEntityWrapperVariant) {
      auto& entity = entity_.template get<kEntityWrapperVariant>();
      entity.ParentObject()->SetMetric(entity.name(),
                                       internal::MakeMetric<T>(value));
    } else if (entity_.index() == kVmoVariant) {
      entity_.template get<kVmoVariant>().Set(value);
    }
  }

  // Add the given value to the value of this numeric metric.
  void Add(T value) {
    if (entity_.index() == kEntityWrapperVariant) {
      auto& entity = entity_.template get<kEntityWrapperVariant>();
      entity.ParentObject()->AddMetric(entity.name(), value);
    } else if (entity_.index() == kVmoVariant) {
      entity_.template get<kVmoVariant>().Add(value);
    }
  }

  // Subtract the given value from the value of this numeric metric.
  void Subtract(T value) {
    if (entity_.index() == kEntityWrapperVariant) {
      auto& entity = entity_.template get<kEntityWrapperVariant>();
      entity.ParentObject()->SubMetric(entity.name(), value);
    } else if (entity_.index() == kVmoVariant) {
      entity_.template get<kVmoVariant>().Subtract(value);
    }
  }

 private:
  friend class ::inspect::Node;

  // Index of the entity wrapper variant of the metric.
  static const int kEntityWrapperVariant = 1;
  // Index of the VMO variant of the metric.
  static const int kVmoVariant = 2;

  // Internal constructor wrapping an entity in memory.
  explicit StaticMetric(internal::EntityWrapper<component::Metric> entity) {
    entity_.template emplace<kEntityWrapperVariant>(std::move(entity));
  }

  // Internal constructor wrapping a VMO type.
  explicit StaticMetric(VmoType entity) {
    entity_.template emplace<kVmoVariant>(std::move(entity));
  }

  fit::internal::variant<fit::internal::monostate,
                         internal::EntityWrapper<component::Metric>, VmoType>
      entity_;
};

template <typename T, typename VmoType>
class ArrayMetric final {
 public:
  // Create a default numeric array metric.
  // Operations on the metric will have no effect.
  ArrayMetric() = default;
  ~ArrayMetric() = default;

  // Allow moving, disallow copying.
  ArrayMetric(const ArrayMetric& other) = delete;
  ArrayMetric(ArrayMetric&& other) = default;
  ArrayMetric& operator=(const ArrayMetric& other) = delete;
  ArrayMetric& operator=(ArrayMetric&& other) = default;

  // Set the value of the given array index.
  void Set(size_t index, T value) { vmo_metric_.Set(index, value); }

  // Add the given value to given array index.
  void Add(size_t index, T value) { vmo_metric_.Add(index, value); }

  // Subtract the given value to the given array index.
  void Subtract(size_t index, T value) { vmo_metric_.Subtract(index, value); }

 private:
  friend class ::inspect::Node;

  // Internal constructor wrapping a VMO type.
  explicit ArrayMetric(VmoType vmo_metric)
      : vmo_metric_(std::move(vmo_metric)) {}

  VmoType vmo_metric_;
};

// Metric wrapping a signed integer.
using IntMetric = StaticMetric<int64_t, vmo::IntMetric>;

// Metric wrapping an unsigned integer.
using UIntMetric = StaticMetric<uint64_t, vmo::UintMetric>;

// Metric wrapping a double floating point number.
using DoubleMetric = StaticMetric<double, vmo::DoubleMetric>;

// Array of signed integers.
using IntArray = ArrayMetric<int64_t, vmo::IntArray>;

// Array of unsigned integers.
using UIntArray = ArrayMetric<uint64_t, vmo::UintArray>;

// Array of double floating point numbers.
using DoubleArray = ArrayMetric<double, vmo::DoubleArray>;

template <typename T, typename VmoType>
class HistogramMetric final {
 public:
  // Create a default histogram.
  // Operations on the metric will have no effect.
  HistogramMetric() = default;
  ~HistogramMetric() = default;

  // Allow moving, disallow copying.
  HistogramMetric(const HistogramMetric& other) = delete;
  HistogramMetric(HistogramMetric&& other) = default;
  HistogramMetric& operator=(const HistogramMetric& other) = delete;
  HistogramMetric& operator=(HistogramMetric&& other) = default;

  // Insert the given value once to the correct bucket of the histogram.
  void Insert(T value) { Insert(value, 1); }

  // Insert the given value |count| times to the correct bucket of the
  // histogram.
  void Insert(T value, T count) { histogram_.Insert(value, count); }

 private:
  friend class ::inspect::Node;

  // Internal constructor wrapping a VMO type.
  HistogramMetric(VmoType histogram) : histogram_(std::move(histogram)) {}

  VmoType histogram_;
};

// Linear histogram of integers.
using LinearIntHistogramMetric =
    HistogramMetric<int64_t, vmo::LinearIntHistogram>;

// Linear histogram of unsigned integers.
using LinearUIntHistogramMetric =
    HistogramMetric<uint64_t, vmo::LinearUintHistogram>;

// Linear histogram of doubles.
using LinearDoubleHistogramMetric =
    HistogramMetric<double, vmo::LinearDoubleHistogram>;

// Exponential histogram of integers.
using ExponentialIntHistogramMetric =
    HistogramMetric<int64_t, vmo::ExponentialIntHistogram>;

// Exponential histogram of unsigned integers.
using ExponentialUIntHistogramMetric =
    HistogramMetric<uint64_t, vmo::ExponentialUintHistogram>;

// Exponential histogram of doubles.
using ExponentialDoubleHistogramMetric =
    HistogramMetric<double, vmo::ExponentialDoubleHistogram>;

// Metric with value determined by evaluating a callback.
class LazyMetric final {
 public:
  // Construct a default metric.
  // Operations on the metric will have no effect.
  LazyMetric();

  LazyMetric(LazyMetric&&) = default;
  LazyMetric(const LazyMetric&) = delete;

  LazyMetric& operator=(LazyMetric&&) = default;
  LazyMetric& operator=(const LazyMetric&) = delete;

  // Set the callback used to return the value of the metric.
  void Set(::component::Metric::ValueCallback callback);

 private:
  friend class ::inspect::Node;
  // Internal constructor setting an actual value on a Node.
  explicit LazyMetric(internal::EntityWrapper<component::Metric> entity);

  fit::optional<internal::EntityWrapper<component::Metric>> entity_;
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
  friend class ::inspect::Node;

  // Index of the entity wrapper variant of the property.
  static const int kEntityWrapperVariant = 1;
  // Index of the VMO variant of the property.
  static const int kVmoVariant = 2;

  // Internal constructor wrapping an entity in memory.
  explicit StringProperty(internal::EntityWrapper<component::Property> entity);

  // Internal constructor wrapping a VMO entity.
  explicit StringProperty(vmo::Property entity);

  fit::internal::variant<fit::internal::monostate,
                         internal::EntityWrapper<component::Property>,
                         vmo::Property>
      entity_;
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
  friend class ::inspect::Node;

  // Index of the entity wrapper variant of the property.
  static const int kEntityWrapperVariant = 1;
  // Index of the VMO variant of the property.
  static const int kVmoVariant = 2;

  // Internal constructor wrapping an entity in memory.
  explicit ByteVectorProperty(
      internal::EntityWrapper<component::Property> entity);

  // Internal constructor wrapping a VMO entity.
  explicit ByteVectorProperty(vmo::Property entity);

  fit::internal::variant<fit::internal::monostate,
                         internal::EntityWrapper<component::Property>,
                         vmo::Property>
      entity_;
};

// Callback type for string values.
using StringValueCallback = ::component::Property::StringValueCallback;

// Property with string value determined by evaluating a callback.
class LazyStringProperty final {
 public:
  // Construct a default property.
  // Operations on the property will have no effect.
  LazyStringProperty();

  LazyStringProperty(LazyStringProperty&&) = default;
  LazyStringProperty(const LazyStringProperty&) = delete;

  LazyStringProperty& operator=(LazyStringProperty&&) = default;
  LazyStringProperty& operator=(const LazyStringProperty&) = delete;

  // Set the callback that generates the value of the property.
  void Set(StringValueCallback callback);

 private:
  friend class ::inspect::Node;

  // Internal constructor setting an actual value on a Node.
  explicit LazyStringProperty(
      internal::EntityWrapper<component::Property> entity);

  fit::optional<internal::EntityWrapper<component::Property>> entity_;
};

// Callback type for vector values.
using VectorValueCallback = ::component::Property::VectorValueCallback;

// Property with byte vector value determined by evaluating a callback.
class LazyByteVectorProperty final {
 public:
  // Construct a default property.
  // Operations on the property will have no effect.
  LazyByteVectorProperty();

  LazyByteVectorProperty(LazyByteVectorProperty&&) = default;
  LazyByteVectorProperty(const LazyByteVectorProperty&) = delete;

  LazyByteVectorProperty& operator=(LazyByteVectorProperty&&) = default;
  LazyByteVectorProperty& operator=(const LazyByteVectorProperty&) = delete;

  // Set the callback that generates the value of the property.
  void Set(VectorValueCallback callback);

 private:
  friend class ::inspect::Node;

  // Internal constructor setting an actual value on a Node.
  explicit LazyByteVectorProperty(
      internal::EntityWrapper<component::Property> entity);

  fit::optional<internal::EntityWrapper<component::Property>> entity_;
};

// Value of vector types, currently an alias for
// component::Property::ByteVector.
using VectorValue = component::Property::ByteVector;

using ChildrenCallbackFunction = ::component::Object::ChildrenCallback;

// ChildrenCallback is an RAII wrapper around a callback attached to a
// Node that provides additional children dynamically.
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
  friend class ::inspect::Node;

  // Internal constructor setting an actual children callback on an object.
  ChildrenCallback(std::shared_ptr<::component::Object> object);

  std::shared_ptr<::component::Object> parent_obj_;
};

// An object under which properties, metrics, and other objects may be nested.
class Node final {
 public:
  // Default construct an empty Node that does nothing until assigned to.
  Node() = default;

  // Construct an object with an explicit name.
  // DEPRECATED: Use Inspector and CreateTree instead of constructing objects
  // directly.
  explicit Node(std::string name);

  // Construct a Node wrapping the given ObjectDir.
  explicit Node(component::ObjectDir object_dir);

  // Construct a Node wrapping the given VMO Object.
  explicit Node(vmo::Object object);

  ~Node() = default;

  // Output the contents of this node as a FIDL struct.
  // For Nodes stored in a VMO, this method returns a default value.
  fuchsia::inspect::Object object() const;

  // Get an ObjectDir wrapping this Node's state.
  // For Nodes stored in a VMO, this method returns a default value.
  component::ObjectDir object_dir() const;

  // Output the list of this node's children as a FIDL-compatible vector.
  // For Nodes stored in a VMO, this method returns a default value.
  ::component::Object::StringOutputVector children() const;

  // Allow moving, disallow copying.
  Node(const Node& other) = delete;
  Node(Node&& other) = default;
  Node& operator=(const Node& other) = delete;
  Node& operator=(Node&& other) = default;

  // Create a new |Node| with the given name that is a child of this node.
  [[nodiscard]] Node CreateChild(std::string name);

  // Create a new |IntMetric| with the given name that is a child of this
  // object.
  [[nodiscard]] IntMetric CreateIntMetric(std::string name, int64_t value);

  // Create a new |UIntMetric| with the given name that is a child of this
  // object.
  [[nodiscard]] UIntMetric CreateUIntMetric(std::string name, uint64_t value);

  // Create a new |DoubleMetric| with the given name that is a child of this
  // object.
  [[nodiscard]] DoubleMetric CreateDoubleMetric(std::string name, double value);

  // Create a new |IntArray| with the given name that is a child of this
  // object.
  [[nodiscard]] IntArray CreateIntArray(std::string name, size_t slots);

  // Create a new |UIntArray| with the given name that is a child of this
  // object.
  [[nodiscard]] UIntArray CreateUIntArray(std::string name, size_t slots);

  // Create a new |DoubleArray| with the given name that is a child of this
  // object.
  [[nodiscard]] DoubleArray CreateDoubleArray(std::string name, size_t slots);

  // Create a new |LinearIntHistogramMetric| with the given name that is a child
  // of this object.
  [[nodiscard]] LinearIntHistogramMetric CreateLinearIntHistogramMetric(
      std::string name, int64_t floor, int64_t step_size, size_t buckets);

  // Create a new |LinearUIntHistogramMetric| with the given name that is a
  // child of this object.
  [[nodiscard]] LinearUIntHistogramMetric CreateLinearUIntHistogramMetric(
      std::string name, uint64_t floor, uint64_t step_size, size_t buckets);

  // Create a new |LinearDoubleHistogramMetric| with the given name that is a
  // child of this object.
  [[nodiscard]] LinearDoubleHistogramMetric CreateLinearDoubleHistogramMetric(
      std::string name, double floor, double step_size, size_t buckets);

  // Create a new |ExponentialIntHistogramMetric| with the given name that is a
  // child of this object.
  [[nodiscard]] ExponentialIntHistogramMetric
  CreateExponentialIntHistogramMetric(std::string name, int64_t floor,
                                      int64_t initial_step,
                                      int64_t step_multiplier, size_t buckets);

  // Create a new |ExponentialIntHistogramMetric| with the given name that is a
  // child of this object.
  [[nodiscard]] ExponentialUIntHistogramMetric
  CreateExponentialUIntHistogramMetric(std::string name, uint64_t floor,
                                       uint64_t initial_step,
                                       uint64_t step_multiplier,
                                       size_t buckets);

  // Create a new |ExponentialDoubleHistogramMetric| with the given name that is
  // a child of this object.
  [[nodiscard]] ExponentialDoubleHistogramMetric
  CreateExponentialDoubleHistogramMetric(std::string name, double floor,
                                         double initial_step,
                                         double step_multiplier,
                                         size_t buckets);

  // Create a new |StringProperty| with the given name that is a child of this
  // object.
  [[nodiscard]] StringProperty CreateStringProperty(std::string name,
                                                    std::string value);

  // Create a new |ByteVectorProperty| with the given name that is a child of
  // this object.
  // For Nodes stored in a VMO, this method has no effect.
  [[nodiscard]] ByteVectorProperty CreateByteVectorProperty(
      std::string name, component::Property::ByteVector value);

  // Create a new |StringCallbackProperty| with the given name that is a child
  // of this object.
  // For Nodes stored in a VMO, this method has no effect.
  [[nodiscard]] LazyStringProperty CreateLazyStringProperty(
      std::string name, component::Property::StringValueCallback callback);

  // Create a new |VectorCallbackProperty| with the given name that is a child
  // of this object.
  // For Nodes stored in a VMO, this method has no effect.
  [[nodiscard]] LazyByteVectorProperty CreateLazyByteVectorProperty(
      std::string name, component::Property::VectorValueCallback callback);

  // Create a new |LazyMetric| with the given name that is a child of this
  // object.
  // For Nodes stored in a VMO, this method has no effect.
  [[nodiscard]] LazyMetric CreateLazyMetric(std::string name,
                                            component::Metric::ValueCallback);

  // Create a new |ChildrenCallback| that dynamically adds children to the
  // object at runtime.
  // For Nodes stored in a VMO, this method has no effect.
  [[nodiscard]] ChildrenCallback CreateChildrenCallback(
      ChildrenCallbackFunction callback);

 private:
  static const int kComponentVariant = 1;
  static const int kVmoVariant = 2;

  // Construct a Node facade in front of an ExposedObject.
  explicit Node(component::ExposedObject object);

  [[nodiscard]] IntArray CreateIntArray(std::string name, size_t slots,
                                        vmo::ArrayFormat format);
  [[nodiscard]] UIntArray CreateUIntArray(std::string name, size_t slots,
                                          vmo::ArrayFormat format);
  [[nodiscard]] DoubleArray CreateDoubleArray(std::string name, size_t slots,
                                              vmo::ArrayFormat format);

  fit::internal::variant<fit::internal::monostate, component::ExposedObject,
                         vmo::Object>
      object_;
};

// Settings to configure a specific Tree.
struct TreeSettings {
  // The initial size of the created VMO.
  size_t initial_size;
  // The maximum size of the created VMO.
  size_t maximum_size;
};

// A |Tree| of inspect objects available in a VMO.
class Tree final {
 public:
  Tree() = default;
  ~Tree();

  // Allow moving, disallow copying.
  Tree(Tree&& other) = default;
  Tree(const Tree& other) = delete;
  Tree& operator=(Tree&& other) = default;
  Tree& operator=(const Tree& other) = delete;

  // Get the root object for this Tree.
  Node& GetRoot() const;

  // Get a reference to the VMO backing this tree.
  const zx::vmo& GetVmo() const;

 private:
  friend class Inspector;

  // Construct a new Tree with the given state;
  Tree(std::unique_ptr<internal::TreeState>);

  // The state for the tree, shared between copies.
  std::unique_ptr<internal::TreeState> state_;
};

// The entry point into the Inspection API.
//
// An Inspector supports creating trees of objects to expose over VMOs.
class Inspector {
 public:
  Inspector() = default;

  // Construct a new tree with the given name and default settings.
  Tree CreateTree(std::string name);

  // Construct a new tree with the given name and settings.
  Tree CreateTree(std::string name, TreeSettings settings);
};

// Generate a unique name with the given prefix.
std::string UniqueName(const std::string& prefix);

}  // namespace inspect

#endif  // LIB_INSPECT_INSPECT_H_
