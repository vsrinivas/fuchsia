// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_DATA_SERIALIZATION_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_DATA_SERIALIZATION_H_

#include "src/lib/fxl/strings/string_view.h"

namespace storage {

template <typename I>
I DeserializeData(fxl::StringView value) {
  static_assert(std::is_trivially_copyable<I>::value,
                "The return type must be trivially copyable.");
  FXL_DCHECK(value.size() == sizeof(I));
  I result;
  memcpy(&result, value.data(), sizeof(I));
  return result;
}

template <typename I>
fxl::StringView SerializeData(const I& value) {
  static_assert(std::is_trivially_copyable<I>::value,
                "The parameter type must be trivially copyable.");
  return fxl::StringView(reinterpret_cast<const char*>(&value), sizeof(I));
}

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_DATA_SERIALIZATION_H_
