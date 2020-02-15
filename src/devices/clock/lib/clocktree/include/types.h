// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CLOCKTREE_TYPES_H_
#define CLOCKTREE_TYPES_H_

#include <stdint.h>

#include <limits>

namespace clk {

using Hertz = uint64_t;
constexpr uint32_t kClkNoParent = std::numeric_limits<uint32_t>::max();

}  // namespace clk

#endif  // CLOCKTREE_TYPES_H_
