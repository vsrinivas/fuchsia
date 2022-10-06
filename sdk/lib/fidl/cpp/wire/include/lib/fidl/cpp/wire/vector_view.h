// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_VECTOR_VIEW_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_VECTOR_VIEW_H_

#include <lib/fidl/cpp/wire/arena.h>
#include <lib/stdcompat/span.h>
#include <zircon/fidl.h>

#include <algorithm>
#include <iterator>
#include <type_traits>

namespace {
class LayoutChecker;
}  // namespace

namespace fidl {

// VectorView is the representation of a FIDL vector in wire domain objects.
//
// VectorViews provide limited functionality to access and set fields of the
// vector and other methods like fidl::Arena, std::array, or std::vector must be
// used to construct it.
//
// VectorView's layout and data format must match fidl_vector_t as it will be
// reinterpret_casted into/from fidl_vector_t during encoding and decoding.
//
// Example:
//
//     uint32_t arr[5] = { 1, 2, 3 };
//     fuchsia_some_lib::wire::SomeFidlObject obj;
//     // Sets the field to a vector view borrowing from |arr|.
//     obj.set_vec_field(fidl::VectorView<uint32_t>::FromExternal(arr));
//
template <typename T>
class VectorView {
 private:
  template <typename>
  friend class VectorView;

 public:
  using elem_type = T;

  VectorView() = default;

  // Allocates a vector using an arena. |T| is default constructed.
  VectorView(AnyArena& allocator, size_t count)
      : count_(count), data_(allocator.AllocateVector<T>(count)) {}
  VectorView(AnyArena& allocator, size_t initial_count, size_t capacity)
      : count_(initial_count), data_(allocator.AllocateVector<T>(capacity)) {
    ZX_DEBUG_ASSERT(initial_count <= capacity);
  }
  VectorView(std::nullptr_t data, size_t count) {}

  // Allocates a vector using an arena and copies the data from the supplied iterators.
  // The iterator must satisfy the random_access_iterator concept.
  //
  // Example:
  //
  //     fidl::Arena arena;
  //     std::vector<int32_t> vec(...);
  //     // Copy contents of |vec| into |arena|, and return a view of the copies content.
  //     fidl::VectorView<int32_t> vv(arena, vec.begin(), vec.end());
  //
  template <typename InputIterator>
  VectorView(AnyArena& arena, InputIterator first, InputIterator last)
      : count_(last - first), data_(arena.AllocateVector<T>(count_)) {
    using Traits = std::iterator_traits<InputIterator>;
    constexpr bool kIsIterator = has_difference_type<Traits>::value;
    static_assert(
        kIsIterator,
        "|InputIterator| is not an iterator. "
        "Ensure that the last two arguments to this constructor are random access iterators.");
    std::copy(first, last, data_);
  }

  // Allocates a vector using an arena and copies the data from the supplied |span|.
  VectorView(AnyArena& arena, cpp20::span<const T> span)
      : VectorView(arena, span.begin(), span.end()) {}

  // Allocates a vector using an arena and copies the data from the supplied |std::vector|.
  VectorView(AnyArena& arena, const std::vector<T>& vector)
      : VectorView(arena, cpp20::span(vector)) {}

  template <typename U>
  VectorView(VectorView<U>&& other) {
    static_assert(
        std::is_same<T, U>::value || std::is_same<T, std::add_const_t<U>>::value,
        "VectorView<T> can only be move-constructed from VectorView<T> or VectorView<const T>");
    count_ = other.count_;
    data_ = other.data_;
  }

  // |FromExternal| methods are the only way to reference data which is not
  // managed by an arena. Their usage is discouraged. The lifetime of the
  // referenced vector must be longer than the lifetime of the created
  // |VectorView|.
  //
  // For example: std::vector<int32_t> my_vector = { 1, 2, 3 }; auto my_view =
  //   fidl::VectorView<int32_t>::FromExternal(my_vector);
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

  cpp20::span<T> get() const { return {data(), count()}; }

  size_t count() const { return count_; }
  void set_count(size_t count) { count_ = count; }

  T* data() const { return data_; }

  // Returns if the vector view is empty.
  bool empty() const { return count() == 0; }

  // TODO(fxbug.dev/109737): |is_null| is used to check if an optional view type
  // is absent. This can be removed if optional view types switch to
  // |fidl::WireOptional|.
  bool is_null() const { return data() == nullptr; }

  T& at(size_t offset) const { return data()[offset]; }
  T& operator[](size_t offset) const { return at(offset); }

  T* begin() const { return data(); }
  const T* cbegin() const { return data(); }

  T* end() const { return data() + count(); }
  const T* cend() const { return data() + count(); }

  // Allocates |count| items of |T| from the |arena|, forgetting any values
  // currently held by the vector view. |T| is default constructed.
  void Allocate(AnyArena& arena, size_t count) {
    count_ = count;
    data_ = arena.AllocateVector<T>(count);
  }

 protected:
  explicit VectorView(std::vector<T>& from) : count_(from.size()), data_(from.data()) {}
  VectorView(T* data, size_t count) : count_(count), data_(data) {}

 private:
  template <class I>
  struct has_difference_type {
    template <class U>
    static std::false_type test(...);
    template <class U>
    static std::true_type test(std::void_t<typename U::difference_type>* = 0);
    static const bool value = decltype(test<I>(0))::value;
  };

  friend ::LayoutChecker;
  size_t count_ = 0;
  T* data_ = nullptr;
};

template <typename T>
VectorView(fidl::AnyArena&, cpp20::span<T>) -> VectorView<T>;

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

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_VECTOR_VIEW_H_
