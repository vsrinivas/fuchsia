// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_CPP_HIERARCHY_H_
#define LIB_INSPECT_CPP_HIERARCHY_H_

#include <limits>
#include <string>
#include <vector>

#include <lib/fit/optional.h>
#include <lib/fit/variant.h>

namespace inspect {

// Describes how an array of values should be displayed.
enum class ArrayDisplayFormat {
  // The array should be displayed as a flat list of numeric types.
  FLAT,

  // The array consists of parameters and buckets for a linear histogram.
  LINEAR_HISTOGRAM,

  // The array consists of parameters and buckets for an exponential
  // histogram.
  EXPONENTIAL_HISTOGRAM,
};

namespace internal {
template <typename T, size_t FormatIndex>
// Internal class wrapping a typed value.
class Value {
 public:
  // Index into the format enum for this type.
  constexpr static size_t format_index = FormatIndex;

  // Construct an empty value.
  Value() = default;

  // Construct a Value wrapping the specific value.
  explicit Value(T value) : value_(std::move(value)) {}

  // Obtain the wrapped value.
  const T& value() const { return value_; }

 private:
  T value_;
};

// An Array is a specialization of Value that contains multiple values as well
// as a display format.
template <typename T, size_t FormatIndex>
class Array final : public Value<std::vector<T>, FormatIndex> {
 public:
  // Describes a single bucket in a histogram.
  //
  // This contains the count of values falling in interval [floor, upper_limit).
  struct HistogramBucket {
    // The floor of values falling in this bucket, inclusive.
    T floor;

    // The upper limit for values falling in this bucket, exclusive.
    T upper_limit;

    // The count of values falling in [floor, upper_limit).
    T count;

    HistogramBucket(T floor, T upper_limit, T count)
        : floor(floor), upper_limit(upper_limit), count(count) {}
  };

  // Constructs an array consisting of values and a display format.
  Array(std::vector<T> values, ArrayDisplayFormat display_format)
      : Value<std::vector<T>, FormatIndex>(std::move(values)), display_format_(display_format) {}

  // Gets the display format for this array.
  ArrayDisplayFormat GetDisplayFormat() const { return display_format_; }

  // Gets the buckets for this array interpreted as a histogram.
  // If the array does not represent a valid histogram, the returned array will
  // be empty.
  std::vector<HistogramBucket> GetBuckets() const;

 private:
  // The display format for this array.
  ArrayDisplayFormat display_format_;
};

template <typename T, size_t FormatIndex>
std::vector<typename Array<T, FormatIndex>::HistogramBucket> Array<T, FormatIndex>::GetBuckets()
    const {
  std::vector<HistogramBucket> ret;

  const auto& value = this->value();

  if (display_format_ == ArrayDisplayFormat::LINEAR_HISTOGRAM) {
    if (value.size() < 5) {
      // We need at least floor, step_size, underflow, bucket 0, overflow.
      return ret;
    }
    T floor = value[0];
    const T step_size = value[1];

    if (std::numeric_limits<T>::has_infinity) {
      ret.push_back(HistogramBucket(-std::numeric_limits<T>::infinity(), floor, value[2]));
    } else {
      ret.push_back(HistogramBucket(std::numeric_limits<T>::min(), floor, value[2]));
    }

    for (size_t i = 3; i < value.size() - 1; i++) {
      ret.push_back(HistogramBucket(floor, floor + step_size, value[i]));
      floor += step_size;
    }

    if (std::numeric_limits<T>::has_infinity) {
      ret.push_back(
          HistogramBucket(floor, std::numeric_limits<T>::infinity(), value[value.size() - 1]));
    } else {
      ret.push_back(HistogramBucket(floor, std::numeric_limits<T>::max(), value[value.size() - 1]));
    }

  } else if (display_format_ == ArrayDisplayFormat::EXPONENTIAL_HISTOGRAM) {
    if (value.size() < 6) {
      // We need at least floor, initial_step, step_multiplier, underflow,
      // bucket 0, overflow.
      return ret;
    }
    T floor = value[0];
    T current_step = value[1];
    const T step_multiplier = value[2];

    if (std::numeric_limits<T>::has_infinity) {
      ret.push_back(HistogramBucket(-std::numeric_limits<T>::infinity(), floor, value[3]));
    } else {
      ret.push_back(HistogramBucket(std::numeric_limits<T>::min(), floor, value[3]));
    }

    for (size_t i = 4; i < value.size() - 1; i++) {
      ret.push_back(HistogramBucket(floor, floor + current_step, value[i]));
      floor += current_step;
      current_step *= step_multiplier;
    }

    if (std::numeric_limits<T>::has_infinity) {
      ret.push_back(
          HistogramBucket(floor, std::numeric_limits<T>::infinity(), value[value.size() - 1]));
    } else {
      ret.push_back(HistogramBucket(floor, std::numeric_limits<T>::max(), value[value.size() - 1]));
    }
  }

  return ret;
}

// Internal class associating a name with one of several types of value.
template <typename TypeVariant, typename FormatType>
class NamedValue final {
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

// Describes the format of a parsed property.
enum class PropertyFormat {
  INVALID = 0,
  INT = 1,
  UINT = 2,
  DOUBLE = 3,
  INT_ARRAY = 4,
  UINT_ARRAY = 5,
  DOUBLE_ARRAY = 6,
  STRING = 7,
  BYTES = 8,
};

using IntPropertyValue = internal::Value<int64_t, static_cast<size_t>(PropertyFormat::INT)>;
using UintPropertyValue = internal::Value<uint64_t, static_cast<size_t>(PropertyFormat::UINT)>;
using DoublePropertyValue = internal::Value<double, static_cast<size_t>(PropertyFormat::DOUBLE)>;
using IntArrayValue = internal::Array<int64_t, static_cast<size_t>(PropertyFormat::INT_ARRAY)>;
using UintArrayValue = internal::Array<uint64_t, static_cast<size_t>(PropertyFormat::UINT_ARRAY)>;
using DoubleArrayValue = internal::Array<double, static_cast<size_t>(PropertyFormat::DOUBLE_ARRAY)>;
using StringPropertyValue =
    internal::Value<std::string, static_cast<size_t>(PropertyFormat::STRING)>;
using ByteVectorPropertyValue =
    internal::Value<std::vector<uint8_t>, static_cast<size_t>(PropertyFormat::BYTES)>;

// Property consists of a name and a value corresponding to one PropertyFormat.
using PropertyValue = internal::NamedValue<
    fit::internal::variant<fit::internal::monostate, IntPropertyValue, UintPropertyValue,
                           DoublePropertyValue, IntArrayValue, UintArrayValue, DoubleArrayValue,
                           StringPropertyValue, ByteVectorPropertyValue>,
    PropertyFormat>;

// A Node stored in a Hierarchy.
class NodeValue final {
 public:
  // Construct an empty NodeValue.
  NodeValue() = default;

  // Construct a NodeValue with a name and no properties.
  explicit NodeValue(std::string name);

  // Construct a NodeValue with a name and properties.
  NodeValue(std::string name, std::vector<PropertyValue> properties);

  // Obtains reference to name.
  const std::string& name() const { return name_; }
  std::string& name() { return name_; }

  // Obtains reference to properties.
  const std::vector<PropertyValue>& properties() const { return properties_; }
  std::vector<PropertyValue>& properties() { return properties_; }

  // Sorts the properties of this node by name.
  void Sort();

 private:
  // The name of this NodeValue.
  std::string name_;

  // The properties for this NodeValue.
  std::vector<PropertyValue> properties_;
};

// Represents a hierarchy of node objects rooted under one particular node.
class Hierarchy final {
 public:
  Hierarchy() = default;

  // Directly construct a hierarchy consisting of a node and a list
  // of children.
  Hierarchy(NodeValue node, std::vector<Hierarchy> children);

  // Allow moving, disallow copying.
  Hierarchy(Hierarchy&&) = default;
  Hierarchy(const Hierarchy&) = delete;
  Hierarchy& operator=(Hierarchy&&) = default;
  Hierarchy& operator=(const Hierarchy&) = delete;

  // Obtains the NodeValue at this level of this hierarchy.
  const NodeValue& node() const { return node_; }
  NodeValue& node() { return node_; }

  // Obtains the name of the Node at this level of the hierarchy.
  const std::string& name() const { return node_.name(); }

  // Gets the children of this object in the hierarchy.
  const std::vector<Hierarchy>& children() const { return children_; }
  std::vector<Hierarchy>& children() { return children_; }

  // Gets a child in this Hierarchy by path.
  // Returns NULL if the requested child could not be found.
  const Hierarchy* GetByPath(std::vector<std::string> path) const;

  // Sort properties and children of this node by name.
  void Sort();

 private:
  NodeValue node_;
  std::vector<Hierarchy> children_;
};
}  // namespace inspect

#endif  // LIB_INSPECT_CPP_HIERARCHY_H_
