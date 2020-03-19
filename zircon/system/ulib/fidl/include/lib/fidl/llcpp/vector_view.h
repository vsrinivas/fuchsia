// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_VECTOR_VIEW_H_
#define LIB_FIDL_LLCPP_VECTOR_VIEW_H_

#include <lib/fidl/walker.h>
#include <zircon/fidl.h>

#include <iterator>
#include <type_traits>

#include "tracking_ptr.h"

namespace {
class LayoutChecker;
}  // namespace

namespace fidl {

// VectorView is the representation of a FIDL vector in LLCPP.
//
// VectorViews provide limited functionality to access and set fields of the
// vector and other objects like fidl::Array or std::vector must be used to
// construct it.
//
// VectorView's layout and data format must match fidl_vector_t as it will be
// reinterpret_casted into fidl_vector_t during linearization.
//
// Example:
// uint32_t arr[5] = { 1, 2, 3 };
// SomeLLCPPObject obj;
// obj.set_vec_field(VectorView(vv));
template <typename T>
class VectorView {
  template <typename>
  friend class VectorView;

  using marked_count = uint64_t;

  // The MSB of count_ stores whether or not data_ is owned by VectorView.
  static constexpr marked_count kOwnershipMask = internal::kVectorOwnershipMask;
  // The maximum count to avoid colliding with the ownership bit.
  static constexpr uint64_t kMaxCount = uint64_t(kOwnershipMask) - 1ULL;

 public:
  VectorView() {}

  VectorView(tracking_ptr<T[]>&& data, uint64_t count) {
    set_data_internal(std::move(data));
    set_count(count);
  }

  // Ideally these constructors wouldn't be needed, but automatic deduction into the tracking_ptr
  // doesn't currently work. A deduction guide can fix this, but it is C++17-only.
  VectorView(unowned_ptr_t<T> data, uint64_t count) : VectorView(tracking_ptr<T[]>(data), count) {}
  template <typename U = T, typename = std::enable_if_t<std::is_const<U>::value>>
  VectorView(unowned_ptr_t<std::remove_const_t<U>> data, uint64_t count)
      : VectorView(tracking_ptr<T[]>(data), count) {}
  VectorView(std::unique_ptr<T[]>&& data, uint64_t count)
      : VectorView(tracking_ptr<T[]>(std::move(data)), count) {}
  VectorView(std::nullptr_t data, uint64_t count) : VectorView(tracking_ptr<T[]>(data), count) {}
  // This constructor triggers a static assertion in tracking_ptr.
  template <typename U, typename = std::enable_if_t<!std::is_array<U>::value>>
  VectorView(U* data, uint64_t count) : VectorView(tracking_ptr<U[]>(data), count) {}

  template <typename U>
  VectorView(VectorView<U>&& other) {
    static_assert(
        std::is_same<T, U>::value || std::is_same<T, std::add_const_t<U>>::value,
        "VectorView<T> can only be move-constructed from VectorView<T> or VectorView<const T>");
    count_ = other.count_;
    data_ = other.data_;
    other.count_ = 0;
    other.data_ = nullptr;
  }

  ~VectorView() {
    if (is_owned()) {
      delete[] data_;
    }
  }

  template <typename U>
  VectorView& operator=(VectorView<U>&& other) {
    static_assert(std::is_same<T, U>::value || std::is_same<T, std::add_const_t<U>>::value,
                  "VectorView<T> can only be assigned from VectorView<T> or VectorView<const T>");
    if (data_ != nullptr && is_owned()) {
      delete[] data_;
    }
    count_ = other.count_;
    data_ = other.data_;
    other.count_ = 0;
    other.data_ = nullptr;
    return *this;
  }

  VectorView(const VectorView&) = delete;
  VectorView& operator=(const VectorView&) = delete;

  uint64_t count() const { return count_ & ~kOwnershipMask; }
  void set_count(uint64_t count) {
    if (count > kMaxCount) {
      abort();
    }
    count_ = count | (count_ & kOwnershipMask);
  }

  const T* data() const { return data_; }
  void set_data(tracking_ptr<T[]> data) { set_data_internal(std::move(data)); }

  T* mutable_data() const { return data_; }

  bool empty() const { return count() == 0; }

  const T& at(size_t offset) const { return data()[offset]; }
  T& at(size_t offset) { return mutable_data()[offset]; }

  const T& operator[](size_t offset) const { return at(offset); }
  T& operator[](size_t offset) { return at(offset); }

  T* begin() { return mutable_data(); }
  const T* begin() const { return data(); }
  const T* cbegin() const { return data(); }

  T* end() { return mutable_data() + count(); }
  const T* end() const { return data() + count(); }
  const T* cend() const { return data() + count(); }

  fidl_vector_t* impl() { return this; }

 private:
  void set_data_internal(tracking_ptr<T[]>&& data) {
    if (data_ != nullptr && is_owned()) {
      delete[] data_;
    }
    if (data.is_owned()) {
      count_ |= kOwnershipMask;
    } else {
      count_ &= ~kOwnershipMask;
    }
    data_ = data.get();
    data.release();
  }

  bool is_owned() const noexcept { return count_ & kOwnershipMask; }

  friend ::LayoutChecker;

  // The lower 63 bits of count_ are reserved to store the number of elements.
  // The MSB stores ownership of the data_ pointer.
  marked_count count_ = 0;
  T* data_ = nullptr;
};

}  // namespace fidl

namespace {
class LayoutChecker {
  static_assert(sizeof(fidl::VectorView<uint8_t>) == sizeof(fidl_vector_t),
                "VectorView size should match fidl_vector_t");
  static_assert(offsetof(fidl::VectorView<uint8_t>, count_) == offsetof(fidl_vector_t, count),
                "VectorView count offset should match fidl_vector_t");
  static_assert(sizeof(fidl::VectorView<uint8_t>::count_) == sizeof(fidl_vector_t::count),
                "VectorView count size should match fidl_vector_t");
  static_assert(offsetof(fidl::VectorView<uint8_t>, data_) == offsetof(fidl_vector_t, data),
                "VectorView data offset should match fidl_vector_t");
  static_assert(sizeof(fidl::VectorView<uint8_t>::data_) == sizeof(fidl_vector_t::data),
                "VectorView data size should match fidl_vector_t");
};

}  // namespace

#endif  // LIB_FIDL_LLCPP_VECTOR_VIEW_H_
