// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_VMO_TYPES_H_
#define LIB_INSPECT_VMO_TYPES_H_

#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <lib/inspect-vmo/block.h>
#include <zircon/types.h>

namespace inspect {
namespace vmo {

class Object;

namespace internal {
class State;

// A metric containing a templated type. All methods wrap the
// corresponding functionality on |internal::State|, and concrete
// implementations are available only for int64_t, uint64_t and double.
template <typename T>
class NumericMetric final {
public:
    // Construct a default numeric metric. Operations on this metric are
    // no-ops.
    NumericMetric() = default;
    ~NumericMetric();

    // Allow moving, disallow copying.
    NumericMetric(const NumericMetric& other) = delete;
    NumericMetric(NumericMetric&& other) = default;
    NumericMetric& operator=(const NumericMetric& other) = delete;
    NumericMetric& operator=(NumericMetric&& other);

    // Set the value of this numeric metric to the given value.
    void Set(T value);

    // Add the given value to the value of this numeric metric.
    void Add(T value);

    // Subtract the given value from the value of this numeric metric.
    void Subtract(T value);

    // Return true if this metric is stored in a buffer. False otherwise.
    explicit operator bool() { return state_.get() != nullptr; }

private:
    friend class internal::State;
    NumericMetric(fbl::RefPtr<internal::State> state, internal::BlockIndex name,
                  internal::BlockIndex value)
        : state_(std::move(state)), name_index_(name), value_index_(value) {}

    // Reference to the state containing this metric.
    fbl::RefPtr<internal::State> state_;

    // Index of the name block in the state.
    internal::BlockIndex name_index_;

    // Index of the value block in the state.
    internal::BlockIndex value_index_;
};

// A value containing an array of numeric types. All methods wrap the
// corresponding functionality on |internal::State|, and concrete
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
    ArrayValue& operator=(ArrayValue&& other);

    // Set the value of the given index of this array.
    void Set(size_t index, T value);

    // Add the given value to the value of this numeric metric.
    void Add(size_t index, T value);

    // Subtract the given value from the value of this numeric metric.
    void Subtract(size_t index, T value);

    // Return true if this metric is stored in a buffer. False otherwise.
    explicit operator bool() { return state_.get() != nullptr; }

private:
    friend class internal::State;
    ArrayValue(fbl::RefPtr<internal::State> state, internal::BlockIndex name,
               internal::BlockIndex value)
        : state_(std::move(state)), name_index_(name), value_index_(value) {}

    // Reference to the state containing this value.
    fbl::RefPtr<internal::State> state_;

    // Index of the name block in the state.
    internal::BlockIndex name_index_;

    // Index of the value block in the state.
    internal::BlockIndex value_index_;
};

} // namespace internal

using IntMetric = internal::NumericMetric<int64_t>;
using UintMetric = internal::NumericMetric<uint64_t>;
using DoubleMetric = internal::NumericMetric<double>;

using IntArray = internal::ArrayValue<int64_t>;
using UintArray = internal::ArrayValue<uint64_t>;
using DoubleArray = internal::ArrayValue<double>;

// A property containing a string value.
// All methods wrap the corresponding functionality on |internal::State|.
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
    Property& operator=(Property&& other);

    // Set the string value of this property.
    void Set(fbl::StringPiece value);

    // Return true if this property is stored in a buffer. False otherwise.
    explicit operator bool() { return state_.get() != nullptr; }

private:
    friend class internal::State;
    Property(fbl::RefPtr<internal::State> state, internal::BlockIndex name,
             internal::BlockIndex value)
        : state_(std::move(state)), name_index_(name), value_index_(value) {}

    // Reference to the state containing this property.
    fbl::RefPtr<internal::State> state_;

    // Index of the name block in the state.
    internal::BlockIndex name_index_;

    // Index of the value block in the state.
    internal::BlockIndex value_index_;
};

// An object under which properties, metrics, and other objects may be nested.
// All methods wrap the corresponding functionality on |internal::State|.
class Object final {
public:
    // Construct a default object. Operations on this object are
    // no-ops.
    Object() = default;
    ~Object();

    // Allow moving, disallow copying.
    Object(const Object& other) = delete;
    Object(Object&& other) = default;
    Object& operator=(const Object& other) = delete;
    Object& operator=(Object&& other);

    // Create a new |Object| with the given name that is a child of this object.
    // If this object is not stored in a buffer, the created object will
    // also not be stored in a buffer.
    [[nodiscard]] Object CreateChild(fbl::StringPiece name);

    // Create a new |IntMetric| with the given name that is a child of this object.
    // If this object is not stored in a buffer, the created metric will
    // also not be stored in a buffer.
    [[nodiscard]] IntMetric CreateIntMetric(fbl::StringPiece name, int64_t value);

    // Create a new |UintMetric| with the given name that is a child of this object.
    // If this object is not stored in a buffer, the created metric will
    // also not be stored in a buffer.
    [[nodiscard]] UintMetric CreateUintMetric(fbl::StringPiece name, uint64_t value);

    // Create a new |DoubleMetric| with the given name that is a child of this object.
    // If this object is not stored in a buffer, the created metric will
    // also not be stored in a buffer.
    [[nodiscard]] DoubleMetric CreateDoubleMetric(fbl::StringPiece name, double value);

    // Create a new |Property| with the given name and format that is a child of this object.
    // If this object is not stored in a buffer, the created property will
    // also not be stored in a buffer.
    [[nodiscard]] Property CreateProperty(fbl::StringPiece name,
                                          fbl::StringPiece value, PropertyFormat format);

    // Create a new |IntArray| with the given name and format that is a child of this object.
    // If this object is not stored in a buffer, the created value will
    // also not be stored in a buffer.
    [[nodiscard]] IntArray CreateIntArray(fbl::StringPiece name, size_t slots, ArrayFormat format);

    // Create a new |UintArray| with the given name and format that is a child of this object.
    // If this object is not stored in a buffer, the created value will
    // also not be stored in a buffer.
    [[nodiscard]] UintArray CreateUintArray(fbl::StringPiece name, size_t slots, ArrayFormat format);

    // Create a new |DoubleArray| with the given name and format that is a child of this object.
    // If this object is not stored in a buffer, the created value will
    // also not be stored in a buffer.
    [[nodiscard]] DoubleArray CreateDoubleArray(fbl::StringPiece name, size_t slots, ArrayFormat format);

    // Return true if this object is stored in a buffer. False otherwise.
    explicit operator bool() { return state_.get() != nullptr; }

private:
    friend class internal::State;
    Object(fbl::RefPtr<internal::State> state, internal::BlockIndex name,
           internal::BlockIndex value)
        : state_(std::move(state)), name_index_(name), value_index_(value) {}

    // Reference to the state containing this metric.
    fbl::RefPtr<internal::State> state_;

    // Index of the name block in the state.
    internal::BlockIndex name_index_;

    // Index of the value block in the state.
    internal::BlockIndex value_index_;
};

} // namespace vmo
} // namespace inspect

#endif // LIB_INSPECT_VMO_TYPES_H_
