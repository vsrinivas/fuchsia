// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTING_FUZZ_DATA_H_
#define SRC_LEDGER_BIN_TESTING_FUZZ_DATA_H_

#include <optional>
#include <string>

namespace ledger {

// Wrapped over fuzz data driving a fuzz test run.
class FuzzData {
 public:
  FuzzData(const uint8_t* data, size_t remaining_size);

  // Returns a small integer or none if there is not enough data left.
  std::optional<uint8_t> GetNextSmallInt();

  // Returns a short string or none if there is not enough data left.
  std::optional<std::string> GetNextShortString();

  // Returns the remaining of the data as a string.
  std::string RemainingString();

 private:
  const uint8_t* data_;
  size_t remaining_size_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTING_FUZZ_DATA_H_
