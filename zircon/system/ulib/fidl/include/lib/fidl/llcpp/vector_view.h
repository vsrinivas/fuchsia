// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_VECTOR_VIEW_H_
#define LIB_FIDL_LLCPP_VECTOR_VIEW_H_

#include <zircon/fidl.h>

#include <iterator>
#include <type_traits>

namespace fidl {

template <typename T>
class VectorView : public fidl_vector_t {
 public:
  VectorView() : fidl_vector_t{} {}
  VectorView(T* data, uint64_t count) : fidl_vector_t{count, data} {}

  // Creates a view over any container that implements |std::data| and |std::size|. For example:
  //
  //     std::vector<Foo> foo_vec = /* ... */;
  //     auto my_view = fidl::VectorView(foo_vec);
  //
  // Note: The constness requirement of C follows that of T, meaning that if the LLCPP call asks for
  // a VectorView<T> where |T| is non-const, this constructor would require a non-const |container|
  // as well.
  template <typename C,
            typename = decltype(std::data(std::declval<C&>())),
            typename = decltype(std::size(std::declval<C&>())),
            typename = std::enable_if_t<std::is_same_v<
                std::is_const<typename std::remove_pointer<
                    decltype(std::data(std::declval<C&>()))>::type>,
                std::is_const<T>>>>
  explicit VectorView(C& container)
      : fidl_vector_t{
            std::size(container),
            // |data| of fidl_vector_t is always |void*|, hence first const cast then static cast.
            static_cast<void*>(
                const_cast<typename std::remove_const<T>::type*>(std::data(container)))} {}

  uint64_t count() const { return fidl_vector_t::count; }
  void set_count(uint64_t count) { fidl_vector_t::count = count; }

  const T* data() const { return static_cast<T*>(fidl_vector_t::data); }
  void set_data(T* data) { fidl_vector_t::data = data; }

  T* mutable_data() const { return static_cast<T*>(fidl_vector_t::data); }

  bool is_null() const { return fidl_vector_t::data == nullptr; }
  bool empty() const { return fidl_vector_t::count == 0; }

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
};

template <typename C,
          typename = decltype(std::data(std::declval<C&>())),
          typename = decltype(std::size(std::declval<C&>()))>
explicit VectorView(C&)
    -> VectorView<typename std::remove_pointer<decltype(std::data(std::declval<C&>()))>::type>;

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_VECTOR_VIEW_H_
