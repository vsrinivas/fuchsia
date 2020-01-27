// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/cpp/inspect.h>

using inspect::internal::ArrayBlockFormat;

namespace inspect {

template <>
internal::NumericProperty<int64_t>::~NumericProperty<int64_t>() {
  if (state_) {
    state_->FreeIntProperty(this);
  }
}

template <>
internal::NumericProperty<int64_t>& internal::NumericProperty<int64_t>::operator=(
    internal::NumericProperty<int64_t>&& other) noexcept {
  if (state_) {
    state_->FreeIntProperty(this);
  }
  state_ = std::move(other.state_);
  name_index_ = other.name_index_;
  value_index_ = other.value_index_;
  return *this;
}

template <>
void internal::NumericProperty<int64_t>::Set(int64_t value) {
  if (state_) {
    state_->SetIntProperty(this, value);
  }
}

template <>
void internal::NumericProperty<int64_t>::Add(int64_t value) {
  if (state_) {
    state_->AddIntProperty(this, value);
  }
}

template <>
void internal::NumericProperty<int64_t>::Subtract(int64_t value) {
  if (state_) {
    state_->SubtractIntProperty(this, value);
  }
}

template <>
internal::NumericProperty<uint64_t>::~NumericProperty<uint64_t>() {
  if (state_) {
    state_->FreeUintProperty(this);
  }
}

template <>
internal::NumericProperty<uint64_t>& internal::NumericProperty<uint64_t>::operator=(
    internal::NumericProperty<uint64_t>&& other) noexcept {
  if (state_) {
    state_->FreeUintProperty(this);
  }
  state_ = std::move(other.state_);
  name_index_ = std::move(other.name_index_);
  value_index_ = std::move(other.value_index_);
  return *this;
}

template <>
void internal::NumericProperty<uint64_t>::Set(uint64_t value) {
  if (state_) {
    state_->SetUintProperty(this, value);
  }
}

template <>
void internal::NumericProperty<uint64_t>::Add(uint64_t value) {
  if (state_) {
    state_->AddUintProperty(this, value);
  }
}

template <>
void internal::NumericProperty<uint64_t>::Subtract(uint64_t value) {
  if (state_) {
    state_->SubtractUintProperty(this, value);
  }
}

template <>
internal::NumericProperty<double>::~NumericProperty<double>() {
  if (state_) {
    state_->FreeDoubleProperty(this);
  }
}

template <>
internal::NumericProperty<double>& internal::NumericProperty<double>::operator=(
    internal::NumericProperty<double>&& other) noexcept {
  if (state_) {
    state_->FreeDoubleProperty(this);
  }
  state_ = std::move(other.state_);
  name_index_ = std::move(other.name_index_);
  value_index_ = std::move(other.value_index_);
  return *this;
}

template <>
void internal::NumericProperty<double>::Set(double value) {
  if (state_) {
    state_->SetDoubleProperty(this, value);
  }
}

template <>
void internal::NumericProperty<double>::Add(double value) {
  if (state_) {
    state_->AddDoubleProperty(this, value);
  }
}

template <>
void internal::NumericProperty<double>::Subtract(double value) {
  if (state_) {
    state_->SubtractDoubleProperty(this, value);
  }
}

template <>
internal::ArrayValue<int64_t>::~ArrayValue<int64_t>() {
  if (state_) {
    state_->FreeIntArray(this);
  }
}

template <>
internal::ArrayValue<int64_t>& internal::ArrayValue<int64_t>::operator=(
    internal::ArrayValue<int64_t>&& other) noexcept {
  if (state_) {
    state_->FreeIntArray(this);
  }
  state_ = std::move(other.state_);
  name_index_ = std::move(other.name_index_);
  value_index_ = std::move(other.value_index_);
  return *this;
}

template <>
void internal::ArrayValue<int64_t>::Set(size_t index, int64_t value) {
  if (state_) {
    state_->SetIntArray(this, index, value);
  }
}

template <>
void internal::ArrayValue<int64_t>::Add(size_t index, int64_t value) {
  if (state_) {
    state_->AddIntArray(this, index, value);
  }
}

template <>
void internal::ArrayValue<int64_t>::Subtract(size_t index, int64_t value) {
  if (state_) {
    state_->SubtractIntArray(this, index, value);
  }
}

template <>
internal::ArrayValue<uint64_t>::~ArrayValue<uint64_t>() {
  if (state_) {
    state_->FreeUintArray(this);
  }
}

template <>
internal::ArrayValue<uint64_t>& internal::ArrayValue<uint64_t>::operator=(
    internal::ArrayValue<uint64_t>&& other) noexcept {
  if (state_) {
    state_->FreeUintArray(this);
  }
  state_ = std::move(other.state_);
  name_index_ = std::move(other.name_index_);
  value_index_ = std::move(other.value_index_);
  return *this;
}

template <>
void internal::ArrayValue<uint64_t>::Set(size_t index, uint64_t value) {
  if (state_) {
    state_->SetUintArray(this, index, value);
  }
}

template <>
void internal::ArrayValue<uint64_t>::Add(size_t index, uint64_t value) {
  if (state_) {
    state_->AddUintArray(this, index, value);
  }
}

template <>
void internal::ArrayValue<uint64_t>::Subtract(size_t index, uint64_t value) {
  if (state_) {
    state_->SubtractUintArray(this, index, value);
  }
}

template <>
internal::ArrayValue<double>::~ArrayValue<double>() {
  if (state_) {
    state_->FreeDoubleArray(this);
  }
}

template <>
internal::ArrayValue<double>& internal::ArrayValue<double>::operator=(
    internal::ArrayValue<double>&& other) noexcept {
  if (state_) {
    state_->FreeDoubleArray(this);
  }
  state_ = std::move(other.state_);
  name_index_ = std::move(other.name_index_);
  value_index_ = std::move(other.value_index_);
  return *this;
}

template <>
void internal::ArrayValue<double>::Set(size_t index, double value) {
  if (state_) {
    state_->SetDoubleArray(this, index, value);
  }
}

template <>
void internal::ArrayValue<double>::Add(size_t index, double value) {
  if (state_) {
    state_->AddDoubleArray(this, index, value);
  }
}

template <>
void internal::ArrayValue<double>::Subtract(size_t index, double value) {
  if (state_) {
    state_->SubtractDoubleArray(this, index, value);
  }
}

#define PROPERTY_METHODS(NAME, TYPE)                                                         \
  template <>                                                                                \
  internal::Property<TYPE>::~Property() {                                                    \
    if (state_) {                                                                            \
      state_->Free##NAME##Property(this);                                                    \
    }                                                                                        \
  }                                                                                          \
                                                                                             \
  template <>                                                                                \
  internal::Property<TYPE>& internal::Property<TYPE>::operator=(Property&& other) noexcept { \
    if (state_) {                                                                            \
      state_->Free##NAME##Property(this);                                                    \
    }                                                                                        \
    state_ = std::move(other.state_);                                                        \
    name_index_ = other.name_index_;                                                         \
    value_index_ = other.value_index_;                                                       \
    return *this;                                                                            \
  }                                                                                          \
                                                                                             \
  template <>                                                                                \
  void internal::Property<TYPE>::Set(const TYPE& value) {                                    \
    if (state_) {                                                                            \
      state_->Set##NAME##Property(this, value);                                              \
    }                                                                                        \
  }

PROPERTY_METHODS(String, std::string)
PROPERTY_METHODS(ByteVector, std::vector<uint8_t>)
PROPERTY_METHODS(Bool, bool)

Node::~Node() {
  if (state_) {
    state_->FreeNode(this);
  }
}

Node& Node::operator=(Node&& other) noexcept {
  if (state_) {
    state_->FreeNode(this);
  }
  state_ = std::move(other.state_);
  name_index_ = std::move(other.name_index_);
  value_index_ = std::move(other.value_index_);
  return *this;
}

Node Node::CreateChild(const std::string& name) {
  if (state_) {
    return state_->CreateNode(name, value_index_);
  }
  return Node();
}

IntProperty Node::CreateInt(const std::string& name, int64_t value) {
  if (state_) {
    return state_->CreateIntProperty(name, value_index_, value);
  }
  return IntProperty();
}

UintProperty Node::CreateUint(const std::string& name, uint64_t value) {
  if (state_) {
    return state_->CreateUintProperty(name, value_index_, value);
  }
  return UintProperty();
}

DoubleProperty Node::CreateDouble(const std::string& name, double value) {
  if (state_) {
    return state_->CreateDoubleProperty(name, value_index_, value);
  }
  return DoubleProperty();
}

BoolProperty Node::CreateBool(const std::string& name, bool value) {
  if (state_) {
    return state_->CreateBoolProperty(name, value_index_, value);
  }
  return BoolProperty();
}

StringProperty Node::CreateString(const std::string& name, const std::string& value) {
  if (state_) {
    return state_->CreateStringProperty(name, value_index_, value);
  }
  return StringProperty();
}

ByteVectorProperty Node::CreateByteVector(const std::string& name,
                                          const std::vector<uint8_t>& value) {
  if (state_) {
    return state_->CreateByteVectorProperty(name, value_index_, value);
  }
  return ByteVectorProperty();
}

IntArray Node::CreateIntArray(const std::string& name, size_t slots) {
  if (state_) {
    return state_->CreateIntArray(name, value_index_, slots, ArrayBlockFormat::kDefault);
  }
  return IntArray();
}

UintArray Node::CreateUintArray(const std::string& name, size_t slots) {
  if (state_) {
    return state_->CreateUintArray(name, value_index_, slots, ArrayBlockFormat::kDefault);
  }
  return UintArray();
}

DoubleArray Node::CreateDoubleArray(const std::string& name, size_t slots) {
  if (state_) {
    return state_->CreateDoubleArray(name, value_index_, slots, ArrayBlockFormat::kDefault);
  }
  return DoubleArray();
}

namespace {
const size_t kExtraSlotsForLinearHistogram = 4;
const size_t kExtraSlotsForExponentialHistogram = 5;
}  // namespace

LinearIntHistogram Node::CreateLinearIntHistogram(const std::string& name, int64_t floor,
                                                  int64_t step_size, size_t buckets) {
  if (state_) {
    const size_t slots = buckets + kExtraSlotsForLinearHistogram;
    auto array =
        state_->CreateIntArray(name, value_index_, slots, ArrayBlockFormat::kLinearHistogram);
    return LinearIntHistogram(floor, step_size, slots, std::move(array));
  }
  return LinearIntHistogram();
}

LinearUintHistogram Node::CreateLinearUintHistogram(const std::string& name, uint64_t floor,
                                                    uint64_t step_size, size_t buckets) {
  if (state_) {
    const size_t slots = buckets + kExtraSlotsForLinearHistogram;
    auto array =
        state_->CreateUintArray(name, value_index_, slots, ArrayBlockFormat::kLinearHistogram);
    return LinearUintHistogram(floor, step_size, slots, std::move(array));
  }
  return LinearUintHistogram();
}

LinearDoubleHistogram Node::CreateLinearDoubleHistogram(const std::string& name, double floor,
                                                        double step_size, size_t buckets) {
  if (state_) {
    const size_t slots = buckets + kExtraSlotsForLinearHistogram;
    auto array =
        state_->CreateDoubleArray(name, value_index_, slots, ArrayBlockFormat::kLinearHistogram);
    return LinearDoubleHistogram(floor, step_size, slots, std::move(array));
  }
  return LinearDoubleHistogram();
}

ExponentialIntHistogram Node::CreateExponentialIntHistogram(const std::string& name, int64_t floor,
                                                            int64_t initial_step,
                                                            int64_t step_multiplier,
                                                            size_t buckets) {
  if (state_) {
    const size_t slots = buckets + kExtraSlotsForExponentialHistogram;
    auto array =
        state_->CreateIntArray(name, value_index_, slots, ArrayBlockFormat::kExponentialHistogram);
    return ExponentialIntHistogram(floor, initial_step, step_multiplier, slots, std::move(array));
  }
  return ExponentialIntHistogram();
}

ExponentialUintHistogram Node::CreateExponentialUintHistogram(const std::string& name,
                                                              uint64_t floor, uint64_t initial_step,
                                                              uint64_t step_multiplier,
                                                              size_t buckets) {
  if (state_) {
    const size_t slots = buckets + kExtraSlotsForExponentialHistogram;
    auto array =
        state_->CreateUintArray(name, value_index_, slots, ArrayBlockFormat::kExponentialHistogram);
    return ExponentialUintHistogram(floor, initial_step, step_multiplier, slots, std::move(array));
  }
  return ExponentialUintHistogram();
}

ExponentialDoubleHistogram Node::CreateExponentialDoubleHistogram(const std::string& name,
                                                                  double floor, double initial_step,
                                                                  double step_multiplier,
                                                                  size_t buckets) {
  if (state_) {
    const size_t slots = buckets + kExtraSlotsForExponentialHistogram;
    auto array = state_->CreateDoubleArray(name, value_index_, slots,
                                           ArrayBlockFormat::kExponentialHistogram);
    return ExponentialDoubleHistogram(floor, initial_step, step_multiplier, slots,
                                      std::move(array));
  }
  return ExponentialDoubleHistogram();
}

std::string Node::UniqueName(const std::string& prefix) { return state_->UniqueName(prefix); }

LazyNode Node::CreateLazyNode(const std::string& name, LazyNodeCallbackFn callback) {
  if (state_) {
    return state_->CreateLazyNode(name, value_index_, std::move(callback));
  }
  return LazyNode();
}

LazyNode Node::CreateLazyValues(const std::string& name, LazyNodeCallbackFn callback) {
  if (state_) {
    return state_->CreateLazyValues(name, value_index_, std::move(callback));
  }
  return LazyNode();
}

Link::~Link() {
  if (state_) {
    state_->FreeLink(this);
  }
}

LazyNode::~LazyNode() {
  if (state_) {
    state_->FreeLazyNode(this);
  }
}

}  // namespace inspect
