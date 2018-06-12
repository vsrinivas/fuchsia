// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace zxdb {

class AddressRange {
 public:
  AddressRange() = default;
  AddressRange(uint64_t begin, uint64_t end);

  uint64_t begin() const { return begin_; }
  uint64_t end() const { return end_; }

  uint64_t size() const { return end_ - begin_; }
  bool empty() const { return end_ == begin_; }

 private:
  uint64_t begin_ = 0;
  uint64_t end_ = 0;
};

// Comparison functor for comparing the beginnings of address ranges.
struct AddressRangeBeginCmp {
  bool operator()(const AddressRange& a, const AddressRange& b) const {
    if (a.begin() == b.begin())
      return a.size() < b.size();
    return a.begin() < b.begin();
  }
};

}  // namespace zxdb
