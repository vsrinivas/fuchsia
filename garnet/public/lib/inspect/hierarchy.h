// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_HIERARCHY_H_
#define LIB_INSPECT_HIERARCHY_H_

#include <fuchsia/inspect/cpp/fidl.h>
#include <lib/fit/optional.h>
#include <lib/fit/variant.h>
#include <lib/inspect-vmo/block.h>

#include <string>
#include <vector>

namespace inspect {

// Namespace hierarchy contains classes representing the parts of a parsed
// ObjectHierarchy.
namespace hierarchy {
namespace internal {
template <typename T, size_t FormatIndex>
// Internal class wrapping a typed value.
class Value {
 public:
  // Index into the format enum for this type.
  constexpr static size_t format_index = FormatIndex;

  // Construct an empty value.
  Value() {}

  // Construct a Value wrapping the specific value.
  explicit Value(T value) : value_(std::move(value)) {}

  // Obtain the wrapped value.
  const T& value() const { return value_; }

 private:
  T value_;
};

// Internal class associating a name with one of several types of value.
template <typename TypeVariant, typename FormatType>
class NamedValue {
 public:
  // Constructs a NamedValue associating the given name with the value.
  template <typename T>
  NamedValue(std::string name, T value) : name_(std::move(name)) {
    format_ = static_cast<FormatType>(T::format_index);
    value_.template emplace<T::format_index>(std::move(value));
  }

  // Checks if this NamedValue contains the templated type.
  template <typename T>
  bool Contains() const {
    return value_.index() == T::format_index;
  }

  // Gets the value by type. If this NamedValue does not contain the given type,
  // this method panics.
  template <typename T>
  const T& Get() const {
    return value_.template get<T::format_index>();
  }

  // Gets the name of this NamedValue.
  const std::string& name() const { return name_; }

  // Gets the format of the wrapped value.
  FormatType format() const { return format_; }

 private:
  FormatType format_;
  std::string name_;
  TypeVariant value_;
};

}  // namespace internal

// Describes the format of a parsed metric.
enum class MetricFormat {
  INVALID = 0,
  INT = 1,
  UINT = 2,
  DOUBLE = 3,
};

using IntMetric =
    internal::Value<int64_t, static_cast<size_t>(MetricFormat::INT)>;
using UIntMetric =
    internal::Value<uint64_t, static_cast<size_t>(MetricFormat::UINT)>;
using DoubleMetric =
    internal::Value<double, static_cast<size_t>(MetricFormat::DOUBLE)>;

// Metric consist of a name and a value corresponding to one MetricFormat.
using Metric = internal::NamedValue<
    fit::internal::variant<fit::internal::monostate, IntMetric, UIntMetric,
                           DoubleMetric>,
    MetricFormat>;

enum class PropertyFormat {
  INVALID = 0,
  STRING = 1,
  BYTES = 2,
};

using StringProperty =
    internal::Value<std::string, static_cast<size_t>(PropertyFormat::STRING)>;
using ByteVectorProperty =
    internal::Value<std::vector<uint8_t>,
                    static_cast<size_t>(PropertyFormat::BYTES)>;

// Property consists of a name and a value corresponding to one PropertyFormat.
using Property = internal::NamedValue<
    fit::internal::variant<fit::internal::monostate, StringProperty,
                           ByteVectorProperty>,
    PropertyFormat>;

// A Node stored in an ObjectHierarchy.
class Node {
 public:
  // Construct an empty Node.
  Node() = default;

  // Construct a Node with a name and no properties or metrics.
  explicit Node(std::string name);

  // Construct a Node with a name, properties, and metrics.
  Node(std::string name, std::vector<Property> properties,
       std::vector<Metric> metrics);

  // Obtains reference to name.
  const std::string& name() const { return name_; }
  std::string& name() { return name_; }

  // Obtains reference to properties.
  const std::vector<Property>& properties() const { return properties_; }
  std::vector<Property>& properties() { return properties_; }

  // Obtains reference to metrics.
  const std::vector<Metric>& metrics() const { return metrics_; }
  std::vector<Metric>& metrics() { return metrics_; }

  // Sorts the metrics and properties of this object by name.
  void Sort();

 private:
  // The name of this Node.
  std::string name_;

  // The properties for this Node.
  std::vector<Property> properties_;

  // The metrics for this Node.
  std::vector<Metric> metrics_;
};
}  // namespace hierarchy

// Represents a hierarchy of node objects rooted under one particular node.
// This class includes constructors that handle reading the hierarchy from
// various sources.
//
// TODO(CF-703): Rename to InspectHierarchy.
class ObjectHierarchy {
 public:
  ObjectHierarchy() = default;

  // Directly construct an object hierarchy consisting of a node and a list
  // of children.
  ObjectHierarchy(hierarchy::Node node, std::vector<ObjectHierarchy> children);

  // Allow moving, disallow copying.
  ObjectHierarchy(ObjectHierarchy&&) = default;
  ObjectHierarchy(const ObjectHierarchy&) = delete;
  ObjectHierarchy& operator=(ObjectHierarchy&&) = default;
  ObjectHierarchy& operator=(const ObjectHierarchy&) = delete;

  // Obtains the Node at this level of this hierarchy.
  const hierarchy::Node& node() const { return node_; }
  hierarchy::Node& node() { return node_; }

  // Gets the children of this object in the hierarchy.
  const std::vector<ObjectHierarchy>& children() const { return children_; };
  std::vector<ObjectHierarchy>& children() { return children_; };

  // Gets a child in this ObjectHierarchy by path.
  // Returns NULL if the requested child could not be found.
  const ObjectHierarchy* GetByPath(std::vector<std::string> path) const;

  // Sort metrics, properties, and children of this object by name.
  void Sort();

 private:
  hierarchy::Node node_;
  std::vector<ObjectHierarchy> children_;
};
}  // namespace inspect

#endif  // LIB_INSPECT_HIERARCHY_H_
