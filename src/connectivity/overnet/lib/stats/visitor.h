// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace overnet {

class StatsVisitor {
 public:
  virtual void Counter(const char* name, uint64_t value) = 0;
};

}  // namespace overnet
