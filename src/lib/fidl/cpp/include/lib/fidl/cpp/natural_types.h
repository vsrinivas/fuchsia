// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_TYPES_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_TYPES_H_

#include <lib/fidl/cpp/coding_traits.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/wire_types.h>
#include <lib/stdcompat/optional.h>

#include <cstdint>

// # Natural domain objects
//
// This header contains forward definitions that are part of natural domain
// objects. The code generator should populate the implementation by generating
// template specializations for each FIDL data type.
namespace fidl {

class Decoder;

template <typename T>
struct CodingTraits<cpp17::optional<std::vector<T>>> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_vector_t);
  static constexpr size_t inline_size_v2 = sizeof(fidl_vector_t);

  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, cpp17::optional<std::vector<T>>* value, size_t offset,
                     cpp17::optional<HandleInformation> maybe_handle_info = cpp17::nullopt) {
    // TODO: encode
  }
  template <typename DecoderImpl>
  static void Decode(DecoderImpl* decoder, cpp17::optional<std::vector<T>>* value, size_t offset) {
    // TODO: decode
  }
};

template <>
struct CodingTraits<cpp17::optional<std::string>> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_string_t);
  static constexpr size_t inline_size_v2 = sizeof(fidl_string_t);

  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, cpp17::optional<std::string>* value, size_t offset,
                     cpp17::optional<HandleInformation> maybe_handle_info = cpp17::nullopt) {
    // TODO: encode
  }
  template <typename DecoderImpl>
  static void Decode(DecoderImpl* decoder, cpp17::optional<std::string>* value, size_t offset) {
    // TODO: decode
  }
};

namespace internal {

// |TypeTraits| contains information about a natural domain object:
//
// - fidl_type_t* kCodingTable: pointer to the coding table.
//
template <typename FidlType>
struct TypeTraits;

// |UnionMemberView| is a helper class for union members.
// It's returned by various accessor methods on union natural domain objects.
// It holds a shared_ptr reference to the underlying variant of the union.

template <size_t I, typename V>
class UnionMemberView final {
 private:
  using Storage = std::shared_ptr<V>;
  Storage storage{};
  using T = std::variant_alternative_t<I, V>;

 public:
  explicit UnionMemberView(Storage storage) : storage(storage) {}

  UnionMemberView& operator=(const T& value) {
    storage->emplace<I>(value);
    return *this;
  }

  UnionMemberView& operator=(T&& value) {
    storage->template emplace<I>(std::move(value));
    return *this;
  }

  // A std::optional-like API:
  explicit operator bool() const noexcept { return has_value(); }
  bool has_value() const noexcept { return storage->index() == I; }

  const T& value() const& {
    const V& variant = *storage;
    return std::get<I>(variant);
  }
  const T* operator->() const& { return std::get_if<I>(storage.get()); }
  T* operator->() & { return std::get_if<I>(storage.get()); }

  template <class U>
  constexpr T value_or(U&& default_value) const& {
    if (storage->index() == I) {
      return value();
    }
    return default_value;
  }
  // TODO: non-const value_or

  // TODO: comparison operators? emplace & swap?

  // Move into a std::optional, invalidating the union.
  std::optional<T> take() && noexcept {
    V v{};
    storage->swap(v);
    if (v.index() == I) {
      return std::make_optional(std::move(std::get<I>(v)));
    }
    return std::nullopt;
  }

  // Copy into an std::optional.
  template <typename U = T, typename = std::enable_if_t<std::is_copy_constructible<U>::value>>
  operator std::optional<T>() const& noexcept {
    if (storage->index() == I) {
      T value = std::get<I>(*storage);
      return std::make_optional(value);
    }
    return std::nullopt;
  }
};

}  // namespace internal
}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_TYPES_H_
