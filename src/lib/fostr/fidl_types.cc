// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fostr/fidl_types.h"

#include "src/lib/fostr/hex_dump.h"

namespace fidl {

template <>
std::ostream& operator<<(std::ostream& os, const VectorPtr<uint8_t>& value) {
  if (!value.has_value()) {
    return os << "<null>";
  }

  if (value.value().empty()) {
    return os << "<empty>";
  }

  if (value.value().size() <= fostr::internal::kMaxBytesToDump) {
    return os << fostr::HexDump(value.value());
  }

  return os << fostr::HexDump(value.value().data(), fostr::internal::kTruncatedDumpSize, 0)
            << fostr::NewLine << "(truncated, " << value.value().size() << " bytes total)";
}

template <>
std::ostream& operator<<(std::ostream& os, const VectorPtr<int8_t>& value) {
  if (!value.has_value()) {
    return os << "<null>";
  }

  if (value.value().empty()) {
    return os << "<empty>";
  }

  if (value.value().size() <= fostr::internal::kMaxBytesToDump) {
    return os << fostr::HexDump(value.value().data(), value.value().size(), 0);
  }

  return os << fostr::HexDump(value.value().data(), fostr::internal::kTruncatedDumpSize, 0)
            << fostr::NewLine << "(truncated, " << value.value().size() << " bytes total)";
}

}  // namespace fidl

namespace fostr {

template <>
std::ostream& operator<<(std::ostream& os, const Formatted<std::vector<uint8_t>>& value) {
  if (value.value.empty()) {
    return os << "<empty>";
  }

  if (value.value.size() <= fostr::internal::kMaxBytesToDump) {
    return os << fostr::HexDump(value.value);
  }

  return os << fostr::HexDump(value.value.data(), fostr::internal::kTruncatedDumpSize, 0)
            << fostr::NewLine << "(truncated, " << value.value.size() << " bytes total)";
}

template <>
std::ostream& operator<<(std::ostream& os, const Formatted<std::vector<int8_t>>& value) {
  if (value.value.empty()) {
    return os << "<empty>";
  }

  if (value.value.size() <= fostr::internal::kMaxBytesToDump) {
    return os << fostr::HexDump(value.value.data(), value.value.size(), 0);
  }

  return os << fostr::HexDump(value.value.data(), fostr::internal::kTruncatedDumpSize, 0)
            << fostr::NewLine << "(truncated, " << value.value.size() << " bytes total)";
}

}  // namespace fostr
