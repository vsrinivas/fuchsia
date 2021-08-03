// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_VECTOR_VIEW_H_
#define LIB_FIDL_LLCPP_VECTOR_VIEW_H_

#include <lib/fidl/llcpp/arena.h>
#include <lib/fidl/walker.h>
#include <zircon/fidl.h>

#include <iterator>
#include <type_traits>

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

 public:
  using elem_type = T;

  VectorView() {}

  // Allocates a vector using an arena.
  VectorView(AnyArena& allocator, size_t count)
      : count_(count), data_(allocator.AllocateVector<T>(count)) {}
  VectorView(AnyArena& allocator, size_t initial_count, size_t capacity)
      : count_(initial_count), data_(allocator.AllocateVector<T>(capacity)) {
    ZX_DEBUG_ASSERT(initial_count <= capacity);
  }
  VectorView(std::nullptr_t data, size_t count) {}

  template <typename U>
  VectorView(VectorView<U>&& other) {
    static_assert(
        std::is_same<T, U>::value || std::is_same<T, std::add_const_t<U>>::value,
        "VectorView<T> can only be move-constructed from VectorView<T> or VectorView<const T>");
    count_ = other.count_;
    data_ = other.data_;
  }

  // These methods are the only way to reference data which is not managed by a Arena.
  // Their usage is discouraged. The lifetime of the referenced vector must be longer than the
  // lifetime of the created VectorView.
  //
  // For example:
  //   std::vector<int32_t> my_vector = { 1, 2, 3 };
  //   auto my_view = fidl::VectorView<int32_t>::FromExternal>(my_vector);
  static VectorView<T> FromExternal(std::vector<T>& from) { return VectorView<T>(from); }
  template <size_t size>
  static VectorView<T> FromExternal(std::array<T, size>& from) {
    return VectorView<T>(from.data(), size);
  }
  template <size_t size>
  static VectorView<T> FromExternal(T (&data)[size]) {
    return VectorView<T>(data, size);
  }
  static VectorView<T> FromExternal(T* data, size_t count) { return VectorView<T>(data, count); }

  template <typename U>
  VectorView& operator=(VectorView<U>&& other) {
    static_assert(std::is_same<T, U>::value || std::is_same<T, std::add_const_t<U>>::value,
                  "VectorView<T> can only be assigned from VectorView<T> or VectorView<const T>");
    count_ = other.count_;
    data_ = other.data_;
    return *this;
  }

  size_t count() const { return count_; }
  void set_count(size_t count) { count_ = count; }

  const T* data() const { return data_; }

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

  void Allocate(AnyArena& allocator, size_t count) {
    count_ = count;
    data_ = allocator.AllocateVector<T>(count);
  }

 protected:
  explicit VectorView(std::vector<T>& from) : count_(from.size()), data_(from.data()) {}
  VectorView(T* data, size_t count) : count_(count), data_(data) {}

 private:
  friend ::LayoutChecker;
  size_t count_ = 0;
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
