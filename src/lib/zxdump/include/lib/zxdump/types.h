// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZXDUMP_INCLUDE_LIB_ZXDUMP_TYPES_H_
#define SRC_LIB_ZXDUMP_INCLUDE_LIB_ZXDUMP_TYPES_H_

#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <cstdint>
#include <iostream>
#include <optional>
#include <string_view>
#include <type_traits>

namespace zxdump {

using ByteView = std::basic_string_view<std::byte>;

// fitx::result<zxdump::Error> is used as the return type of many operations.
// It carries a zx_status_t and a string describing what operation failed.
struct Error {
  std::string_view status_string() const;

  std::string_view op_;
  zx_status_t status_{};
};

// This is a shorthand for the return type of the Dump function passed to
// various <lib/zxdump/dump.>h APIs.
template <typename Dump>
using DumpResult = std::decay_t<decltype(std::declval<Dump>()(size_t{}, ByteView{}))>;

// fitx::result<zxdump::DumpError<Dump>> is used as the return type of
// operations that take a Dump function.  Usually either Error::status_ will be
// ZX_OK and dump_error_ will be set, or dump_error_ will be std::nullopt and
// Error::status_ will not be ZX_OK.  For dump errors, Error::op_ will be the
// name of the <lib/zxdump/dump.h> method rather than the Zircon operation.
template <typename Dump>
struct DumpError : public Error {
  constexpr DumpError() = default;
  constexpr DumpError(const DumpError&) = default;
  constexpr DumpError(DumpError&&) noexcept = default;

  explicit constexpr DumpError(const Error& other) : Error{other} {}

  constexpr DumpError& operator=(const DumpError&) = default;
  constexpr DumpError& operator=(DumpError&&) noexcept = default;

  std::optional<std::decay_t<decltype(std::declval<DumpResult<Dump>>().error_value())>> dump_error_;
};

}  // namespace zxdump

// This prints "op: status" with the status string.
std::ostream& operator<<(std::ostream& os, const zxdump::Error& error);

// This does either that or `os << "op: " << error.dump_error_`.  If a
// particular error_value type of a Dump function is too generic for
// std::ostream::operator<< to be specialized as desired for it, then
// this can be explicitly specialized for the Dump type.
template <typename Dump>
inline std::ostream& operator<<(std::ostream& os, const zxdump::DumpError<Dump>& error) {
  using namespace std::literals;
  if (error.status_ == ZX_OK) {
    ZX_DEBUG_ASSERT(error.dump_error_.has_value());
    return os << error.op_ << ": "sv << error.dump_error_.value();
  }
  return os << static_cast<const zxdump::Error&>(error);
}

#endif  // SRC_LIB_ZXDUMP_INCLUDE_LIB_ZXDUMP_TYPES_H_
