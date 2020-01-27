// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_CPP_VMO_TYPES_H_
#define LIB_INSPECT_CPP_VMO_TYPES_H_

#include <lib/fit/function.h>
#include <lib/fit/promise.h>
#include <lib/inspect/cpp/vmo/block.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <string>
#include <vector>

namespace inspect {
class Node;
class Inspector;

using LazyNodeCallbackFn = fit::function<fit::promise<Inspector>()>;

namespace internal {
class State;

// A property containing a templated numeric type. All methods wrap the
// corresponding functionality on |State|, and concrete
// implementations are available only for int64_t, uint64_t and double.
template <typename T>
class NumericProperty final {
 public:
  // Construct a default numeric metric. Operations on this metric are
  // no-ops.
  NumericProperty() = default;
  ~NumericProperty();

  // Allow moving, disallow copying.
  NumericProperty(const NumericProperty& other) = delete;
  NumericProperty(NumericProperty&& other) = default;
  NumericProperty& operator=(const NumericProperty& other) = delete;
  NumericProperty& operator=(NumericProperty&& other) noexcept;

  // Set the value of this numeric metric to the given value.
  void Set(T value);

  // Add the given value to the value of this numeric metric.
  void Add(T value);

  // Subtract the given value from the value of this numeric metric.
  void Subtract(T value);

  // Return true if this metric is stored in a buffer. False otherwise.
  explicit operator bool() { return state_ != nullptr; }

 private:
  friend class ::inspect::internal::State;
  NumericProperty(std::shared_ptr<internal::State> state, internal::BlockIndex name,
                  internal::BlockIndex value)
      : state_(std::move(state)), name_index_(name), value_index_(value) {}

  // Reference to the state containing this metric.
  std::shared_ptr<internal::State> state_;

  // Index of the name block in the state.
  internal::BlockIndex name_index_;

  // Index of the value block in the state.
  internal::BlockIndex value_index_;
};

// A value containing an array of numeric types. All methods wrap the
// corresponding functionality on |State|, and concrete
// implementations are available only for int64_t, uint64_t and double.
template <typename T>
class ArrayValue final {
 public:
  // Construct a default array value. Operations on this value are
  // no-ops.
  ArrayValue() = default;
  ~ArrayValue();

  // Allow moving, disallow copying.
  ArrayValue(const ArrayValue& other) = delete;
  ArrayValue(ArrayValue&& other) = default;
  ArrayValue& operator=(const ArrayValue& other) = delete;
  ArrayValue& operator=(ArrayValue&& other) noexcept;

  // Set the value of the given index of this array.
  void Set(size_t index, T value);

  // Add the given value to the value of this numeric metric.
  void Add(size_t index, T value);

  // Subtract the given value from the value of this numeric metric.
  void Subtract(size_t index, T value);

  // Return true if this metric is stored in a buffer. False otherwise.
  explicit operator bool() { return state_ != nullptr; }

 private:
  friend class ::inspect::internal::State;
  ArrayValue(std::shared_ptr<internal::State> state, internal::BlockIndex name,
             internal::BlockIndex value)
      : state_(std::move(state)), name_index_(name), value_index_(value) {}

  // Reference to the state containing this value.
  std::shared_ptr<internal::State> state_;

  // Index of the name block in the state.
  internal::BlockIndex name_index_;

  // Index of the value block in the state.
  internal::BlockIndex value_index_;
};

template <typename T>
class LinearHistogram final {
 public:
  // Create a default histogram.
  // Operations on the metric will have no effect.
  LinearHistogram() = default;

  // Movable but not copyable.
  LinearHistogram(const LinearHistogram& other) = delete;
  LinearHistogram(LinearHistogram&& other) = default;
  LinearHistogram& operator=(const LinearHistogram& other) = delete;
  LinearHistogram& operator=(LinearHistogram&& other) = default;

  // Insert the given value once to the correct bucket of the histogram.
  void Insert(T value) { Insert(value, 1); }

  // Insert the given value |count| times to the correct bucket of the
  // histogram.
  void Insert(T value, T count) { array_.Add(GetIndexForValue(value), count); }

 private:
  friend class ::inspect::Node;

  // First slots are floor, step_size, and underflow.
  static const size_t kBucketOffset = 3;

  // Get the number of buckets, which excludes the two parameter slots and the
  // two overflow slots.
  size_t BucketCount() { return array_size_ - 4; }

  // Calculates the correct array index to store the given value.
  size_t GetIndexForValue(T value) {
    if (array_size_ == 0) {
      return 0;
    }
    size_t ret = kBucketOffset - 1;
    T current_floor = floor_;
    for (; value >= current_floor && ret < array_size_ - 1; current_floor += step_size_, ret++) {
    }
    return ret;
  }

  // Internal constructor wrapping an array.
  LinearHistogram(T floor, T step_size, size_t array_size, ArrayValue<T> array)
      : floor_(floor), step_size_(step_size), array_size_(array_size), array_(std::move(array)) {
    ZX_ASSERT(array_size_ > 4);
    array_.Set(0, floor_);
    array_.Set(1, step_size_);
  }

  T floor_ = 0;
  T step_size_ = 0;
  size_t array_size_ = 0;
  ArrayValue<T> array_;
};

template <typename T>
class ExponentialHistogram final {
 public:
  // Create a default histogram.
  // Operations on the metric will have no effect.
  ExponentialHistogram() = default;

  // Movable but not copyable.
  ExponentialHistogram(const ExponentialHistogram& other) = delete;
  ExponentialHistogram(ExponentialHistogram&& other) = default;
  ExponentialHistogram& operator=(const ExponentialHistogram& other) = delete;
  ExponentialHistogram& operator=(ExponentialHistogram&& other) = default;

  // Insert the given value once to the correct bucket of the histogram.
  void Insert(T value) { Insert(value, 1); }

  // Insert the given value |count| times to the correct bucket of the
  // histogram.
  void Insert(T value, T count) { array_.Add(GetIndexForValue(value), count); }

 private:
  friend class ::inspect::Node;

  // First slots are floor, initial_step, step_multiplier, and underflow.
  static const size_t kBucketOffset = 4;

  // Get the number of buckets, which excludes the two parameter slots and the
  // two overflow slots.
  size_t BucketCount() { return array_size_ - 5; }

  // Calculates the correct array index to store the given value.
  size_t GetIndexForValue(T value) {
    if (array_size_ == 0) {
      return 0;
    }
    T current_floor = floor_;
    T current_step = initial_step_;
    size_t ret = kBucketOffset - 1;
    while (value >= current_floor && ret < array_size_ - 1) {
      current_floor = floor_ + current_step;
      current_step *= step_multiplier_;
      ret++;
    }
    return ret;
  }

  // Internal constructor wrapping a VMO type.
  ExponentialHistogram(T floor, T initial_step, T step_multiplier, size_t array_size,
                       ArrayValue<T> array)
      : floor_(floor),
        initial_step_(initial_step),
        step_multiplier_(step_multiplier),
        array_size_(array_size),
        array_(std::move(array)) {
    ZX_ASSERT(array_size_ > 5);
    array_.Set(0, floor_);
    array_.Set(1, initial_step_);
    array_.Set(2, step_multiplier_);
  }

  T floor_ = 0;
  T initial_step_ = 0;
  T step_multiplier_ = 0;
  size_t array_size_ = 0;
  ArrayValue<T> array_;
};

// A property containing a string value.
// All methods wrap the corresponding functionality on |State|.
template <typename T>
class Property final {
 public:
  // Construct a default property. Operations on this property are
  // no-ops.
  Property() = default;
  ~Property();

  // Allow moving, disallow copying.
  Property(const Property& other) = delete;
  Property(Property&& other) = default;
  Property& operator=(const Property& other) = delete;
  Property& operator=(Property&& other) noexcept;

  // Return true if this property is stored in a buffer. False otherwise.
  explicit operator bool() { return state_ != nullptr; }

  // Set the value of this property.
  void Set(const T& value);

 private:
  friend class ::inspect::internal::State;
  Property(std::shared_ptr<internal::State> state, internal::BlockIndex name,
           internal::BlockIndex value)
      : state_(std::move(state)), name_index_(name), value_index_(value) {}

  // Reference to the state containing this property.
  std::shared_ptr<internal::State> state_;

  // Index of the name block in the state.
  internal::BlockIndex name_index_;

  // Index of the value block in the state.
  internal::BlockIndex value_index_;
};

}  // namespace internal

using IntProperty = internal::NumericProperty<int64_t>;
using UintProperty = internal::NumericProperty<uint64_t>;
using DoubleProperty = internal::NumericProperty<double>;
using BoolProperty = internal::Property<bool>;

using IntArray = internal::ArrayValue<int64_t>;
using UintArray = internal::ArrayValue<uint64_t>;
using DoubleArray = internal::ArrayValue<double>;

using LinearIntHistogram = internal::LinearHistogram<int64_t>;
using LinearUintHistogram = internal::LinearHistogram<uint64_t>;
using LinearDoubleHistogram = internal::LinearHistogram<double>;

using ExponentialIntHistogram = internal::ExponentialHistogram<int64_t>;
using ExponentialUintHistogram = internal::ExponentialHistogram<uint64_t>;
using ExponentialDoubleHistogram = internal::ExponentialHistogram<double>;

using StringProperty = internal::Property<std::string>;
using ByteVectorProperty = internal::Property<std::vector<uint8_t>>;

// Links specify a location that can be read as a continuation of an Inspect hierarchy.
class Link final {
 public:
  // Construct a default link.
  Link() = default;
  ~Link();

  // Allow moving, disallow copying.
  Link(const Link& other) = delete;
  Link(Link&& other) = default;
  Link& operator=(const Link& other) = delete;
  Link& operator=(Link&& other) = default;

  // Return true if this node is stored in a buffer. False otherwise.
  explicit operator bool() { return state_ != nullptr; }

 private:
  friend class ::inspect::internal::State;
  Link(std::shared_ptr<internal::State> state, internal::BlockIndex name,
       internal::BlockIndex value, internal::BlockIndex content)
      : state_(std::move(state)), name_index_(name), value_index_(value), content_index_(content) {}

  // Reference to the state containing this value.
  std::shared_ptr<internal::State> state_;

  // Index of the name block in the state.
  internal::BlockIndex name_index_;

  // Index of the value block in the state.
  internal::BlockIndex value_index_;

  // Index of the content block in the state.
  internal::BlockIndex content_index_;
};

// A LazyNode has a value that is dynamically set by a callback.
class LazyNode final {
 public:
  // Construct a default LazyNode.
  LazyNode() = default;
  ~LazyNode();

  // Allow moving, disallow copying.
  LazyNode(const LazyNode& other) = delete;
  LazyNode(LazyNode&& other) = default;
  LazyNode& operator=(const LazyNode& other) = delete;
  LazyNode& operator=(LazyNode&& other) = default;

  // Return true if this value is represented in a buffer. False otherwise.
  explicit operator bool() { return state_ != nullptr; }

 private:
  friend class ::inspect::internal::State;
  LazyNode(std::shared_ptr<internal::State> state, std::string content_value, Link link)
      : state_(std::move(state)),
        content_value_(std::move(content_value)),
        link_(std::move(link)) {}

  // Reference to the state containing this value.
  std::shared_ptr<internal::State> state_;

  // The value stored in the contents of the Link for this node. Used as a key for removal when
  // deleted.
  std::string content_value_;

  // The Link node that references this LazyNode.
  Link link_;
};

// A node under which properties, metrics, and other nodes may be nested.
// All methods wrap the corresponding functionality on |State|.
class Node final {
 public:
  // Construct a default node. Operations on this node are
  // no-ops.
  Node() = default;
  ~Node();

  // Allow moving, disallow copying.
  Node(const Node& other) = delete;
  Node(Node&& other) = default;
  Node& operator=(const Node& other) = delete;
  Node& operator=(Node&& other) noexcept;

  // Create a new |Node| with the given name that is a child of this node.
  // If this node is not stored in a buffer, the created node will
  // also not be stored in a buffer.
  Node CreateChild(const std::string& name) __WARN_UNUSED_RESULT;

  // Same as CreateChild, but emplaces the value in the given container.
  //
  // The type of |list| must have method emplace(Node).
  // inspect::ValueList is recommended for most use cases.
  template <typename T>
  void CreateChild(const std::string& name, T* list) {
    list->emplace(CreateChild(name));
  }

  // Create a new |IntProperty| with the given name that is a child of this node.
  // If this node is not stored in a buffer, the created metric will
  // also not be stored in a buffer.
  IntProperty CreateInt(const std::string& name, int64_t value) __WARN_UNUSED_RESULT;

  // Same as CreateInt, but emplaces the value in the given container.
  //
  // The type of |list| must have method emplace(IntProperty).
  // inspect::ValueList is recommended for most use cases.
  template <typename T>
  void CreateInt(const std::string& name, int64_t value, T* list) {
    list->emplace(CreateInt(name, value));
  }

  // Create a new |UintProperty| with the given name that is a child of this node.
  // If this node is not stored in a buffer, the created metric will
  // also not be stored in a buffer.
  UintProperty CreateUint(const std::string& name, uint64_t value) __WARN_UNUSED_RESULT;

  // Same as CreateUint, but emplaces the value in the given container.
  //
  // The type of |list| must have method emplace(UintProperty).
  // inspect::ValueList is recommended for most use cases.
  template <typename T>
  void CreateUint(const std::string& name, uint64_t value, T* list) {
    list->emplace(CreateUint(name, value));
  }

  // Create a new |DoubleProperty| with the given name that is a child of this node.
  // If this node is not stored in a buffer, the created metric will
  // also not be stored in a buffer.
  DoubleProperty CreateDouble(const std::string& name, double value) __WARN_UNUSED_RESULT;

  // Same as CreateDouble, but emplaces the value in the given container.
  //
  // The type of |list| must have method emplace(DoubleProperty).
  // inspect::ValueList is recommended for most use cases.
  template <typename T>
  void CreateDouble(const std::string& name, double value, T* list) {
    list->emplace(CreateDouble(name, value));
  }

  // Create a new |BoolProperty| with the given name that is a child of this node.
  // If this node is not stored in a buffer, the created metric will
  // also not be stored in a buffer.
  BoolProperty CreateBool(const std::string& name, bool value) __WARN_UNUSED_RESULT;

  // Same as CreateBool, but emplaces the value in the given container.
  //
  // The type of |list| must have method emplace(BoolProperty).
  // inspect::ValueList is recommended for most use cases.
  template <typename T>
  void CreateBool(const std::string& name, bool value, T* list) {
    list->emplace(CreateBool(name, value));
  }

  // Create a new |StringProperty| with the given name and value that is a child of this node.
  // If this node is not stored in a buffer, the created property will
  // also not be stored in a buffer.
  StringProperty CreateString(const std::string& name,
                              const std::string& value) __WARN_UNUSED_RESULT;

  // Same as CreateString, but emplaces the value in the given container.
  //
  // The type of |list| must have method emplace(StringProperty).
  // inspect::ValueList is recommended for most use cases.
  template <typename T>
  void CreateString(const std::string& name, const std::string& value, T* list) {
    list->emplace(CreateString(name, value));
  }

  // Create a new |ByteVectorProperty| with the given name and value that is a child of this node.
  // If this node is not stored in a buffer, the created property will
  // also not be stored in a buffer.
  ByteVectorProperty CreateByteVector(const std::string& name,
                                      const std::vector<uint8_t>& value) __WARN_UNUSED_RESULT;

  // Same as CreateByteVector, but emplaces the value in the given container.
  //
  // The type of |list| must have method emplace(ByteVectorProperty).
  // inspect::ValueList is recommended for most use cases.
  template <typename T>
  void CreateByteVector(const std::string& name, const std::vector<uint8_t>& value, T* list) {
    list->emplace(CreateByteVector(name, value));
  }

  // Create a new |IntArray| with the given name and slots that is a child of this node.
  // If this node is not stored in a buffer, the created value will
  // also not be stored in a buffer.
  IntArray CreateIntArray(const std::string& name, size_t slots) __WARN_UNUSED_RESULT;

  // Create a new |UintArray| with the given name and slots that is a child of this node.
  // If this node is not stored in a buffer, the created value will
  // also not be stored in a buffer.
  UintArray CreateUintArray(const std::string& name, size_t slots) __WARN_UNUSED_RESULT;

  // Create a new |DoubleArray| with the given name and slots that is a child of this node.
  // If this node is not stored in a buffer, the created value will
  // also not be stored in a buffer.
  DoubleArray CreateDoubleArray(const std::string& name, size_t slots) __WARN_UNUSED_RESULT;

  // Create a new |LinearIntHistogram| with the given name and format that is a child of this
  // node. If this node is not stored in a buffer, the created value will also not be stored in
  // a buffer.
  LinearIntHistogram CreateLinearIntHistogram(const std::string& name, int64_t floor,
                                              int64_t step_size,
                                              size_t buckets) __WARN_UNUSED_RESULT;

  // Create a new |LinearUintHistogram| with the given name and format that is a child of this
  // node. If this node is not stored in a buffer, the created value will also not be stored in
  // a buffer.
  LinearUintHistogram CreateLinearUintHistogram(const std::string& name, uint64_t floor,
                                                uint64_t step_size,
                                                size_t buckets) __WARN_UNUSED_RESULT;

  // Create a new |LinearDoubleHistogram| with the given name and format that is a child of this
  // node. If this node is not stored in a buffer, the created value will also not be stored in
  // a buffer.
  LinearDoubleHistogram CreateLinearDoubleHistogram(const std::string& name, double floor,
                                                    double step_size,
                                                    size_t buckets) __WARN_UNUSED_RESULT;

  // Create a new |ExponentialIntHistogram| with the given name and format that is a child of this
  // node. If this node is not stored in a buffer, the created value will also not be stored in
  // a buffer.
  ExponentialIntHistogram CreateExponentialIntHistogram(const std::string& name, int64_t floor,
                                                        int64_t initial_step,
                                                        int64_t step_multiplier,
                                                        size_t buckets) __WARN_UNUSED_RESULT;

  // Create a new |ExponentialUintHistogram| with the given name and format that is a child of this
  // node. If this node is not stored in a buffer, the created value will also not be stored in
  // a buffer.
  ExponentialUintHistogram CreateExponentialUintHistogram(const std::string& name, uint64_t floor,
                                                          uint64_t initial_step,
                                                          uint64_t step_multiplier,
                                                          size_t buckets) __WARN_UNUSED_RESULT;

  // Create a new |ExponentialDoubleHistogram| with the given name and format that is a child of
  // this node. If this node is not stored in a buffer, the created value will also not be
  // stored in a buffer.
  ExponentialDoubleHistogram CreateExponentialDoubleHistogram(const std::string& name, double floor,
                                                              double initial_step,
                                                              double step_multiplier,
                                                              size_t buckets) __WARN_UNUSED_RESULT;

  // Create a new |LazyNode| with the given name that is populated by the given callback on demand.
  //
  // The passed |callback| will live as long as the returned LazyNode, and will not be called
  // concurrently by multiple threads.
  //
  // For example:
  //  auto a = root.CreateChild("a");
  //  a.CreateLazyNode("b", [] {
  //    Inspector insp;
  //    ValueList values;
  //    insp.GetRoot().CreateInt("val", 2, &values);
  //    return fit::make_ok_result(insp);
  //  });
  //
  //  Output:
  //  root:
  //    a:
  //      b:
  //        val = 2
  LazyNode CreateLazyNode(const std::string& name,
                          LazyNodeCallbackFn callback) __WARN_UNUSED_RESULT;

  // Same as CreateLazyNode, but emplaces the value in the given container.
  //
  // The type of |list| must have method emplace(LazyNode).
  // inspect::ValueList is recommended for most use cases.
  template <typename F, typename T>
  void CreateLazyNode(const std::string& name, F callback, T* list) {
    list->emplace(CreateLazyNode(name, std::move(callback)));
  }

  // Create a new |LazyNode| whose children and properties are added to this node on demand.
  //
  // The passed |callback| will live as long as the returned LazyNode, and will not be called
  // concurrently by multiple threads.
  //
  // The name is only used if inflating the tree callback fails.
  //
  // WARNING: It is the caller's responsibility to avoid name collisions with other properties
  // on this node.
  //
  // For example:
  //  auto a = root.CreateChild("a");
  //  a.CreateLazy("b", [] {
  //    Inspector insp;
  //    ValueList values;
  //    insp.GetRoot().CreateInt("val", 2).enlist(&values);
  //    return fit::make_ok_promise(insp);
  //  });
  //
  //  Output:
  //  root:
  //    a:
  //      val = 2
  //
  //  Alternatively:
  //
  //  a.CreateLazyNode("b", [] {
  //    return fit::make_error_promise();
  //  });
  //
  //  Possible output:
  //  root:
  //    a:
  //      b [Failed to open link]
  LazyNode CreateLazyValues(const std::string& name,
                            LazyNodeCallbackFn callback) __WARN_UNUSED_RESULT;

  // Same as CreateLazyValues, but emplaces the value in the given container.
  //
  // The type of |list| must have method emplace(LazyNode).
  // inspect::ValueList is recommended for most use cases.
  template <typename F, typename T>
  void CreateLazyValues(const std::string& name, F callback, T* list) {
    list->emplace(CreateLazyValues(name, std::move(callback)));
  }

  // Create a new |LazyNode| whose children and properties are added to this node on demand.
  // Return true if this node is stored in a buffer. False otherwise.
  explicit operator bool() { return state_ != nullptr; }

  // Create a unique name for children of this node.
  //
  // The returned strings are guaranteed to be at least unique within the context of this Node.
  std::string UniqueName(const std::string& prefix);

 private:
  friend class ::inspect::internal::State;
  Node(std::shared_ptr<internal::State> state, internal::BlockIndex name,
       internal::BlockIndex value)
      : state_(std::move(state)), name_index_(name), value_index_(value) {}

  // Reference to the state containing this metric.
  std::shared_ptr<internal::State> state_;

  // Index of the name block in the state.
  internal::BlockIndex name_index_;

  // Index of the value block in the state.
  internal::BlockIndex value_index_;
};

}  // namespace inspect

#endif  // LIB_INSPECT_CPP_VMO_TYPES_H_
