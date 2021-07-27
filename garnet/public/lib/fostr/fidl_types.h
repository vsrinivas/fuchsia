// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FOSTR_FIDL_TYPES_H_
#define LIB_FOSTR_FIDL_TYPES_H_

#include <array>
#include <sstream>

#include "garnet/public/lib/fostr/hex_dump.h"
#include "lib/fidl/cpp/vector.h"
#include "lib/fostr/indent.h"

#ifdef __Fuchsia__
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/interface_handle.h"
#include "lib/fidl/cpp/interface_ptr.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fostr/zx_types.h"
#endif

namespace fostr {

// Wrapper type to disambiguate formatting operator overloads.
//
// This file avoids defining any overloads for types in the std namespace. To correctly format
// arrays, vectors and unique pointers, this wrapper is used so we can define an overload for
// e.g. fostr::Formatted<std::unique_ptr<T>> instead of defining one for std::unique_ptr<T>.
// Consequently, this wrapper must be used for the supported std types. The wrapper has no effect
// for other types, so it can safely be applied to any value.
//
//     std::vector<int32_t> my_fector;
//     os << fostr::Formatted(my_vector);
//
template <typename T>
struct Formatted {
  explicit Formatted(const T& v) : value(v) {}
  const T& value;
};

namespace internal {

static constexpr size_t kMaxBytesToDump = 256;
static constexpr size_t kTruncatedDumpSize = 64;

template <typename Iter>
void insert_sequence_container(std::ostream& os, Iter begin, Iter end) {
  if (begin == end) {
    os << "<empty>";
  }

  int index = 0;
  for (; begin != end; ++begin) {
    os << NewLine << "[" << index++ << "] " << Indent << Formatted(*begin) << Outdent;
  }
}

}  // namespace internal

template <typename T>
std::ostream& operator<<(std::ostream& os, const Formatted<T>& value);

template <typename T>
std::ostream& operator<<(std::ostream& os, const Formatted<T>& value) {
  return os << value.value;
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const Formatted<std::unique_ptr<T>>& value) {
  if (!value.value) {
    return os << "<null>";
  }

  return os << Formatted(*value.value);
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const Formatted<std::vector<T>>& value) {
  if (value.value.empty()) {
    return os << "<empty>";
  }

  internal::insert_sequence_container(os, value.value.cbegin(), value.value.cend());
  return os;
}

template <>
std::ostream& operator<<(std::ostream& os, const Formatted<std::vector<uint8_t>>& value);

template <>
std::ostream& operator<<(std::ostream& os, const Formatted<std::vector<int8_t>>& value);

template <typename T, size_t N>
std::ostream& operator<<(std::ostream& os, const Formatted<std::array<T, N>>& value);

template <typename T, size_t N, size_t M>
std::ostream& operator<<(std::ostream& os,
                         const Formatted<std::array<std::array<T, M>, N>>& value) {
  if (value.value.empty()) {
    return os << "<empty>";
  }

  int index = 0;
  for (const std::array<T, M>& item : value.value) {  // N items
    os << NewLine << "[" << index++ << "]:" << Indent << Formatted(item) << Outdent;
  }
  return os;
}

template <typename T, size_t N>
std::ostream& operator<<(std::ostream& os, const Formatted<std::array<T, N>>& value) {
  internal::insert_sequence_container(os, value.value.cbegin(), value.value.cend());
  return os;
}

template <size_t N>
std::ostream& operator<<(std::ostream& os, const Formatted<std::array<uint8_t, N>>& value) {
  if (N <= internal::kMaxBytesToDump) {
    return os << HexDump(value.value.data(), N, 0);
  }

  return os << HexDump(value.value.data(), internal::kTruncatedDumpSize, 0) << NewLine
            << "(truncated, " << N << " bytes total)";
}

template <size_t N>
std::ostream& operator<<(std::ostream& os, const Formatted<std::array<int8_t, N>>& value) {
  if (N <= internal::kMaxBytesToDump) {
    return os << HexDump(value.value.data(), N, 0);
  }

  return os << HexDump(value.value.data(), internal::kTruncatedDumpSize, 0) << NewLine
            << "(truncated, " << N << " bytes total)";
}

}  // namespace fostr

namespace fidl {

// ostream operator<< templates for fidl types. These templates conform to the
// convention described in indent.h.

template <typename T>
std::ostream& operator<<(std::ostream& os, const VectorPtr<T>& value) {
  if (!value.has_value()) {
    return os << "<null>";
  }

  if (value.value().empty()) {
    return os << "<empty>";
  }

  fostr::internal::insert_sequence_container(os, value.value().cbegin(), value.value().cend());
  return os;
}

template <>
std::ostream& operator<<(std::ostream& os, const VectorPtr<uint8_t>& value);

template <>
std::ostream& operator<<(std::ostream& os, const VectorPtr<int8_t>& value);

#ifdef __Fuchsia__
template <typename T>
std::ostream& operator<<(std::ostream& os, const Binding<T>& value) {
  if (!value.is_bound()) {
    return os << "<not bound>";
  }

  return os << value.channel();
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const InterfaceHandle<T>& value) {
  if (!value.is_valid()) {
    return os << "<not valid>";
  }

  return os << value.channel();
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const InterfacePtr<T>& value) {
  if (!value.is_bound()) {
    return os << "<not bound>";
  }

  return os << value.channel();
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const InterfaceRequest<T>& value) {
  if (!value.is_valid()) {
    return os << "<not valid>";
  }

  return os << value.channel();
}
#endif

}  // namespace fidl

#endif  // LIB_FOSTR_FIDL_TYPES_H_
