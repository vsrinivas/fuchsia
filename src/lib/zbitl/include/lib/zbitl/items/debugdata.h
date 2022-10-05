// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_ITEMS_DEBUGDATA_H_
#define SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_ITEMS_DEBUGDATA_H_

#include <lib/fit/result.h>
#include <lib/stdcompat/span.h>

#include <cstdint>
#include <string_view>

namespace zbitl {

// This provides trivial decoding of ZBI_TYPE_DEBUGDATA item payloads.
// The main contents blob and the three strings each have accessors.
//
// The Init() method always takes a `const` payload, but the mutable_contents()
// method will return it as a mutable span that can be used if it's appropriate
// to modify the original payload data in place.
class Debugdata {
 public:
  // This fails if the header is invalid, meaning it's missing or truncated, or
  // its sizes add up to more than the payload size available.
  fit::result<std::string_view> Init(cpp20::span<const std::byte> payload);

  cpp20::span<const std::byte> contents() const { return contents_; }

  cpp20::span<std::byte> mutable_contents() const {
    return {const_cast<std::byte*>(contents_.data()), contents_.size_bytes()};
  }

  std::string_view sink_name() const { return sink_name_; }
  std::string_view vmo_name() const { return vmo_name_; }
  std::string_view log() const { return log_; }

 private:
  std::string_view sink_name_, vmo_name_, log_;
  cpp20::span<const std::byte> contents_;
};

}  // namespace zbitl

#endif  // SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_ITEMS_DEBUGDATA_H_
