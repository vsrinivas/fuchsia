// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FOSTR_FIDL_TYPES_H_
#define LIB_FOSTR_FIDL_TYPES_H_

#include "garnet/public/lib/fostr/hex_dump.h"
#include "lib/fidl/cpp/array.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/interface_handle.h"
#include "lib/fidl/cpp/interface_ptr.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fidl/cpp/vector.h"
#include "lib/fostr/indent.h"
#include "lib/fostr/zx_types.h"

namespace fostr {
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
    os << NewLine << "[" << index++ << "] " << *begin;
  }
}

}  // namespace internal
}  // namespace fostr

namespace fidl {

// ostream operator<< templates for fidl types. These templates conform to the
// convention described in indent.h.

template <typename T, size_t N>
std::ostream& operator<<(std::ostream& os, const Array<T, N>& value) {
  fostr::internal::insert_sequence_container(os, value.cbegin(), value.cend());
  return os;
}

template <size_t N>
std::ostream& operator<<(std::ostream& os, const Array<uint8_t, N>& value) {
  if (N <= fostr::internal::kMaxBytesToDump) {
    return os << fostr::HexDump(value.data(), N, 0);
  }

  return os << fostr::HexDump(value.data(), fostr::internal::kTruncatedDumpSize,
                              0)
            << fostr::NewLine << "(truncated, " << N << " bytes total)";
}

template <size_t N>
std::ostream& operator<<(std::ostream& os, const Array<int8_t, N>& value) {
  if (N <= fostr::internal::kMaxBytesToDump) {
    return os << fostr::HexDump(value.data(), N, 0);
  }

  return os << fostr::HexDump(value.data(), fostr::internal::kTruncatedDumpSize,
                              0)
            << fostr::NewLine << "(truncated, " << N << " bytes total)";
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const VectorPtr<T>& value) {
  if (value.is_null()) {
    return os << "<null>";
  }

  if (value.get().empty()) {
    return os << "<empty>";
  }

  fostr::internal::insert_sequence_container(os, value.get().cbegin(),
                                             value.get().cend());
  return os;
}

template <>
std::ostream& operator<<(std::ostream& os, const VectorPtr<uint8_t>& value);

template <>
std::ostream& operator<<(std::ostream& os, const VectorPtr<int8_t>& value);

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

}  // namespace fidl

#endif  // LIB_FOSTR_FIDL_TYPES_H_
