// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_SIZED_DATA_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_SIZED_DATA_H_

#include <cstdint>
#include <vector>

namespace forensics {

// Move-only specialization of std::vector that can be used in place of std::vector when
// the data in the underlying buffer is copyable, but copying it is undesirable, e.g. the vector
// holds a large amount of data.
class SizedData : public std::vector<uint8_t> {
 public:
  // Inherit the constructors of std::vector.
  using std::vector<uint8_t>::vector;

  // Delete copy constructors.
  SizedData(const SizedData& other) = delete;
  SizedData& operator=(const SizedData& other) = delete;

  // Define move constructors as defaults.
  SizedData(SizedData&& other) = default;
  SizedData& operator=(SizedData&& other) = default;
};

}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_SIZED_DATA_H_
