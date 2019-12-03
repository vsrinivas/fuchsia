// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_CPP_HIERARCHY_H_
#define LIB_INSPECT_CPP_HIERARCHY_H_

#include <lib/fit/function.h>
#include <lib/fit/optional.h>
#include <lib/fit/variant.h>

#include <limits>
#include <map>
#include <string>
#include <vector>

namespace inspect {

// Describes how an array of values should be displayed.
enum class ArrayDisplayFormat : uint8_t {
  // The array should be displayed as a flat list of numeric types.
  kFlat,

  // The array consists of parameters and buckets for a linear histogram.
  kLinearHistogram,

  // The array consists of parameters and buckets for an exponential
  // histogram.
  kExponentialHistogram,
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

  Value(const Value&) = delete;
  Value(Value&&) = default;
  Value& operator=(const Value&) = delete;
  Value& operator=(Value&&) = default;

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
  struct HistogramBucket final {
    // The floor of values falling in this bucket, inclusive.
    T floor;

    // The upper limit for values falling in this bucket, exclusive.
    T upper_limit;

    // The count of values falling in [floor, upper_limit).
    T count;

    HistogramBucket(T floor, T upper_limit, T count)
        : floor(floor), upper_limit(upper_limit), count(count) {}

    bool operator==(const HistogramBucket& other) const {
      return floor == other.floor && upper_limit == other.upper_limit && count == other.count;
    }

    bool operator!=(const HistogramBucket& other) const { return !((*this) == other); }
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

  if (display_format_ == ArrayDisplayFormat::kLinearHistogram) {
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

  } else if (display_format_ == ArrayDisplayFormat::kExponentialHistogram) {
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

    T current_floor = floor;
    T offset = current_step;
    for (size_t i = 4; i < value.size() - 1; i++) {
      T upper = floor + offset;
      ret.push_back(HistogramBucket(current_floor, upper, value[i]));
      offset *= step_multiplier;
      current_floor = upper;
    }

    if (std::numeric_limits<T>::has_infinity) {
      ret.push_back(HistogramBucket(current_floor, std::numeric_limits<T>::infinity(),
                                    value[value.size() - 1]));
    } else {
      ret.push_back(
          HistogramBucket(current_floor, std::numeric_limits<T>::max(), value[value.size() - 1]));
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

// The disposition for a LinkValue describes how its contents should be included in the parent node.
enum LinkDisposition {
  // Include the linked Tree as a child of the parent node.
  kChild = 0,
  // Inline all children of the linked Tree's root as children of the parent node.
  kInline = 1,
};

// Wrapper for a particular LINK_VALUE.
class LinkValue final {
 public:
  explicit LinkValue(std::string name, std::string content, LinkDisposition disposition)
      : name_(std::move(name)), content_(std::move(content)), disposition_(disposition) {}

  const std::string& name() const { return name_; }
  const std::string& content() const { return content_; }
  LinkDisposition disposition() const { return disposition_; }

 private:
  std::string name_;
  std::string content_;
  LinkDisposition disposition_;
};

// Describes the format of a parsed property.
enum class PropertyFormat : uint8_t {
  kInvalid = 0,
  kInt = 1,
  kUint = 2,
  kDouble = 3,
  kIntArray = 4,
  kUintArray = 5,
  kDoubleArray = 6,
  kString = 7,
  kBytes = 8,
};

using IntPropertyValue = internal::Value<int64_t, static_cast<size_t>(PropertyFormat::kInt)>;
using UintPropertyValue = internal::Value<uint64_t, static_cast<size_t>(PropertyFormat::kUint)>;
using DoublePropertyValue = internal::Value<double, static_cast<size_t>(PropertyFormat::kDouble)>;
using IntArrayValue = internal::Array<int64_t, static_cast<size_t>(PropertyFormat::kIntArray)>;
using UintArrayValue = internal::Array<uint64_t, static_cast<size_t>(PropertyFormat::kUintArray)>;
using DoubleArrayValue = internal::Array<double, static_cast<size_t>(PropertyFormat::kDoubleArray)>;
using StringPropertyValue =
    internal::Value<std::string, static_cast<size_t>(PropertyFormat::kString)>;
using ByteVectorPropertyValue =
    internal::Value<std::vector<uint8_t>, static_cast<size_t>(PropertyFormat::kBytes)>;

// Property consists of a name and a value corresponding to one PropertyFormat.
using PropertyValue = internal::NamedValue<
    fit::internal::variant<fit::internal::monostate, IntPropertyValue, UintPropertyValue,
                           DoublePropertyValue, IntArrayValue, UintArrayValue, DoubleArrayValue,
                           StringPropertyValue, ByteVectorPropertyValue>,
    PropertyFormat>;

// A Node parsed from a Hierarchy.
//
// This is named NodeValue to differentiate it from Node, the write-side definition of nodes.
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

  // Sets the name.
  void set_name(std::string name) { name_ = std::move(name); }

  // Obtains reference to properties.
  const std::vector<PropertyValue>& properties() const { return properties_; }

  // Takes the properties, leaving the vector owned by this node blank.
  std::vector<PropertyValue> take_properties() {
    std::vector<PropertyValue> ret;
    ret.swap(properties_);
    return ret;
  }

  // Adds a property to this node.
  void add_property(PropertyValue property) { properties_.emplace_back(std::move(property)); }

  // Obtains reference to links.
  const std::vector<LinkValue>& links() const { return links_; }

  // Adds a link to this node.
  void add_link(LinkValue link) { links_.emplace_back(std::move(link)); }

  // Sets the vector of links for this node.
  void set_links(std::vector<LinkValue> links) { links_ = std::move(links); }

  // Sorts the properties of this node by name.
  //
  // See description of Hierarchy::Sort.
  void Sort();

 private:
  // The name of this NodeValue.
  std::string name_;

  // The properties for this NodeValue.
  std::vector<PropertyValue> properties_;

  // The links for this NodeValue.
  std::vector<LinkValue> links_;
};

enum class MissingValueReason {
  // A referenced hierarchy in a link was not found.
  kLinkNotFound = 1,

  // A linked hierarchy at this location could not be parsed successfully.
  kLinkHierarchyParseFailure = 2,

  // A link we attempted to follow was not properly formatted, or its format is not known to this
  // reader.
  kLinkInvalid = 3,
};

// Wrapper for a value that was missing at a location in the hierarchy.
struct MissingValue {
  MissingValue() = default;
  MissingValue(MissingValueReason reason, std::string name)
      : reason(reason), name(std::move(name)) {}

  // The reason why the value is missing.
  MissingValueReason reason;

  // The name of the missing value.
  std::string name;
};

// Represents a hierarchy of node objects rooted under one particular node.
//
// NodeValues do not contain children because they are parsed directly from a buffer. Hierarchies
// provide a wrapper around a hierarchy of nodes including named children. They additionally provide
// links to hierarchies that can be parsed and spliced in from nested files.
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

  // Obtains a pointer to the underlying NodeValue.
  NodeValue* node_ptr() { return &node_; }

  // Obtains the name of the Node at this level of the hierarchy.
  const std::string& name() const { return node_.name(); }

  // Gets the children of this object in the hierarchy.
  const std::vector<Hierarchy>& children() const { return children_; }

  // Takes the children from this hierarchy.
  std::vector<Hierarchy> take_children() { return std::move(children_); }

  // Adds a child to this hierarchy.
  void add_child(Hierarchy child) { children_.emplace_back(std::move(child)); }

  // Gets the list of missing values for this location in the hierarchy.
  const std::vector<MissingValue>& missing_values() const { return missing_values_; }

  // Adds a missing value for this location in the hierarchy.
  void add_missing_value(MissingValueReason reason, std::string name) {
    missing_values_.emplace_back(reason, std::move(name));
  }

  // Gets a child in this Hierarchy by path.
  // Returns nullptr if the requested child could not be found.
  //
  // The returned pointer will be invalidated if the Hierarchy is modified.
  const Hierarchy* GetByPath(const std::vector<std::string>& path) const;

  // Visit all descendents of this Hierarchy, calling the given callback with a mutable pointer to
  // each child.
  //
  // Traversal stops when all descendents are visited or the callback returns false.
  void Visit(fit::function<bool(const std::vector<std::string>&, Hierarchy*)> callback);

  // Visit all descendents of this Hierarchy, calling the given callback with a const pointer to
  // each child.
  //
  // Traversal stops when all descendents are visited or the callback returns false.
  void Visit(fit::function<bool(const std::vector<std::string>&, const Hierarchy*)> callback) const;

  // Sort properties and children of this node by, and recursively sort each child.
  //
  // This method imposes a canonical ordering on every child value in the hierarchy for purposes of
  // comparison and output. It does not optimize operations in any way.
  //
  // The sorting rule for each of children and property values is as follows:
  // - If and only if all names match non-negative integral strings, sort numerically.
  // - Otherwise, sort lexicographically.
  //
  // For example:
  //   3b 2 1 11 -> 1 11 2 3b
  //   2 1 11 3  -> 1 2 3 11
  //   -1 3 20   -> -1 20 3
  void Sort();

 private:
  NodeValue node_;
  std::vector<Hierarchy> children_;
  std::vector<MissingValue> missing_values_;
};
}  // namespace inspect

#endif  // LIB_INSPECT_CPP_HIERARCHY_H_
