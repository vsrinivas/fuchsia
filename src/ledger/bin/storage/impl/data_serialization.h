// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_DATA_SERIALIZATION_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_DATA_SERIALIZATION_H_

#include <initializer_list>
#include <string>

#include "src/lib/fxl/logging.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

template <typename I>
I DeserializeData(absl::string_view value) {
  static_assert(std::is_trivially_copyable<I>::value,
                "The return type must be trivially copyable.");
  FXL_DCHECK(value.size() == sizeof(I));
  I result;
  memcpy(&result, value.data(), sizeof(I));
  return result;
}

template <typename I>
absl::string_view SerializeData(const I& value) {
  static_assert(std::is_trivially_copyable<I>::value,
                "The parameter type must be trivially copyable.");
  return absl::string_view(reinterpret_cast<const char*>(&value), sizeof(I));
}

// Similar to fxl::Concatenate, but additionally inserts the length as a prefix to each of the
// StringViews. Prevents accidental collisions.
std::string SafeConcatenation(std::initializer_list<absl::string_view> string_views);

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_DATA_SERIALIZATION_H_
