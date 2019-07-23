// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <type_traits>

namespace fidl {

template <class T, class Enable = void>
struct ToEmbeddedTraits;

template <class T>
struct ToEmbeddedTraits<T,
                        typename std::enable_if<IsPrimitive<T>::value>::type> {
  static T Lift(T value) { return value; }
};

template <>
struct ToEmbeddedTraits<std::string> {
  static const std::string& Lift(const std::string& value) { return value; }
};

template <class T>
struct ToEmbeddedTraits<std::vector<T>> {
  using EmbeddedT = typename std::decay<decltype(
      ToEmbeddedTraits<T>::Lift(std::declval<T>()))>::type;
  static auto Lift(const std::vector<T>& value) -> std::vector<EmbeddedT> {
    std::vector<EmbeddedT> out;
    for (const auto& in : value) {
      out.emplace_back(ToEmbeddedTraits<T>::Lift(in));
    }
    return out;
  }
};

template <class T, int N>
struct ToEmbeddedTraits<std::array<T, N>> {
  using EmbeddedT = typename std::decay<decltype(
      ToEmbeddedTraits<T>::Lift(std::declval<T>()))>::type;
  static auto Lift(const std::array<T, N>& value) -> std::array<EmbeddedT, N> {
    std::array<EmbeddedT, N> out;
    for (size_t i = 0; i < N; i++) {
      out[i] = std::move(ToEmbeddedTraits<T>::Lift(value[i]));
    }
    return out;
  }
};

template <class T>
inline auto ToEmbedded(const T& source) {
  return ToEmbeddedTraits<T>::Lift(source);
}

}  // namespace fidl
