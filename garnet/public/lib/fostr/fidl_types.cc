// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/fostr/fidl_types.h"

#include "garnet/public/lib/fostr/hex_dump.h"

namespace fidl {

template <>
std::ostream& operator<<(std::ostream& os, const VectorPtr<uint8_t>& value) {
  if (value.is_null()) {
    return os << "<null>";
  }

  if (value.get().empty()) {
    return os << "<empty>";
  }

  if (value.get().size() <= fostr::internal::kMaxBytesToDump) {
    return os << fostr::HexDump(value.get());
  }

  return os << fostr::HexDump(value.get().data(),
                              fostr::internal::kTruncatedDumpSize, 0)
            << fostr::NewLine << "(truncated, " << value.get().size()
            << " bytes total)";
}

template <>
std::ostream& operator<<(std::ostream& os, const VectorPtr<int8_t>& value) {
  if (value.is_null()) {
    return os << "<null>";
  }

  if (value.get().empty()) {
    return os << "<empty>";
  }

  if (value.get().size() <= fostr::internal::kMaxBytesToDump) {
    return os << fostr::HexDump(value.get().data(), value.get().size(), 0);
  }

  return os << fostr::HexDump(value.get().data(),
                              fostr::internal::kTruncatedDumpSize, 0)
            << fostr::NewLine << "(truncated, " << value.get().size()
            << " bytes total)";
}

}  // namespace fidl
