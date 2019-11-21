// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/data_serialization.h"

#include <initializer_list>
#include <string>

#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

std::string SafeConcatenation(std::initializer_list<absl::string_view> string_views) {
  std::string result;
  size_t result_size = string_views.size() * sizeof(size_t);
  for (const absl::string_view& string_view : string_views) {
    result_size += string_view.size();
  }
  result.reserve(result_size);
  for (const absl::string_view& string_view : string_views) {
    result.append(storage::SerializeData(string_view.size()).data(), sizeof(size_t));
    result.append(string_view.data(), string_view.size());
  }
  return result;
}

}  // namespace storage
