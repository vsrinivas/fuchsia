// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/ordinals.h"

#include <optional>

#define BORINGSSL_NO_CXX
#include <openssl/sha.h>

namespace fidl {
namespace ordinals {

std::string GetSelector(const raw::AttributeList* attributes, SourceSpan name) {
  if (attributes != nullptr) {
    const size_t size = attributes->attributes.size();
    for (size_t i = 0; i < size; i++) {
      if (attributes->attributes[i].name == "Selector") {
        return attributes->attributes[i].value;
      }
    }
  }
  return std::string(name.data().data(), name.data().size());
}

namespace {

uint64_t CalcOrdinal(const std::string_view& full_name) {
  uint8_t digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const uint8_t*>(full_name.data()), full_name.size(), digest);
  // The following dance ensures that we treat the bytes as a little-endian
  // int64 regardless of host byte order.
  // clang-format off
  uint64_t ordinal =
      static_cast<uint64_t>(digest[0]) |
      static_cast<uint64_t>(digest[1]) << 8 |
      static_cast<uint64_t>(digest[2]) << 16 |
      static_cast<uint64_t>(digest[3]) << 24 |
      static_cast<uint64_t>(digest[4]) << 32 |
      static_cast<uint64_t>(digest[5]) << 40 |
      static_cast<uint64_t>(digest[6]) << 48 |
      static_cast<uint64_t>(digest[7]) << 56;
  // clang-format on

  return ordinal & 0x7fffffffffffffff;
}

}  // namespace

raw::Ordinal64 GetGeneratedOrdinal64(const std::vector<std::string_view>& library_name,
                                     const std::string_view& protocol_name,
                                     const std::string_view& selector_name,
                                     const raw::SourceElement& source_element) {
  if (selector_name.find("/") != selector_name.npos)
    return raw::Ordinal64(source_element, CalcOrdinal(selector_name));

  // TODO(pascallouis): Move this closer (code wise) to NameFlatName, ideally
  // sharing code.
  std::string full_name;
  bool once = false;
  for (std::string_view id : library_name) {
    if (once) {
      full_name.push_back('.');
    } else {
      once = true;
    }
    full_name.append(id.data(), id.size());
  }
  // TODO(pascallouis/yifeit): Remove this once fuchsia.io has been renamed to
  // fuchsia.io1.
  //
  // In order to make room for the new fuchsia.io library (dubbed fuchsia.io2
  // currently), we are piggybacking the rename of the currently named
  // `fuchsia.io` library to `fuchsia.io1`. In short, from an ABI standpoint,
  // both `fuchsia.io` and `fuchsia.io1` are the same.
  if (full_name == "fuchsia.io") {
    full_name = "fuchsia.io1";
  }
  full_name.append("/");
  full_name.append(protocol_name.data(), protocol_name.size());
  full_name.append(".");
  full_name.append(selector_name);

  return raw::Ordinal64(source_element, CalcOrdinal(full_name));
}

}  // namespace ordinals
}  // namespace fidl
