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
  using marked_count = uint64_t;

  // The MSB of count_ stores whether or not data_ is owned by VectorView.
  static constexpr marked_count kOwnershipMask = internal::kVectorOwnershipMask;
  // The maximum count to avoid colliding with the ownership bit.
  static constexpr uint64_t kMaxCount = uint64_t(kOwnershipMask) - 1ULL;

 public:
  VectorView() {}
  VectorView(T* data, uint64_t count) {
    set_data_internal(fidl::unowned(data));
    set_count(count);
  }

  VectorView(VectorView&& other) {
    count_ = other.count_;
    data_ = other.data_;
    other.count_ = 0;
    other.data_ = nullptr;
  }

  VectorView& operator=(VectorView&& other) {
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

  // Creates a view over any container that implements |std::data| and |std::size|. For example:
  //
  //     std::vector<Foo> foo_vec = /* ... */;
  //     auto my_view = fidl::VectorView(foo_vec);
  //
  // Note: The constness requirement of C follows that of T, meaning that if the LLCPP call asks for
  // a VectorView<T> where |T| is non-const, this constructor would require a non-const |container|
  // as well.
  template <
      typename C, typename = decltype(std::data(std::declval<C&>())),
      typename = decltype(std::size(std::declval<C&>())),
      typename = std::enable_if_t<std::is_same<std::is_const<typename std::remove_pointer<
                                                   decltype(std::data(std::declval<C&>()))>::type>,
                                               std::is_const<T>>::value>>
  explicit VectorView(C& container) {
    set_count(std::size(container));
    set_data_internal(fidl::unowned(std::data(container)));
  }

  uint64_t count() const { return count_ & ~kOwnershipMask; }
  void set_count(uint64_t count) {
    if (count > kMaxCount) {
      abort();
    }
    count_ = count | (count_ & kOwnershipMask);
  }

  const T* data() const { return data_; }
  void set_data(T* data) { set_data_internal(fidl::unowned(data)); }

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
  void set_data_internal(tracking_ptr<T[]> data) {
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

template <typename C, typename = decltype(std::data(std::declval<C&>())),
          typename = decltype(std::size(std::declval<C&>()))>
explicit VectorView(C&)
    -> VectorView<typename std::remove_pointer<decltype(std::data(std::declval<C&>()))>::type>;

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
