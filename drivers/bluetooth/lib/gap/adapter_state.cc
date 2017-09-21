// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "adapter_state.h"

namespace bluetooth {
namespace gap {

AdapterState::AdapterState() : max_lmp_feature_page_index_(0) {
  std::memset(supported_commands_, 0, sizeof(supported_commands_));
  std::memset(lmp_features_, 0, sizeof(lmp_features_));
}

}  // namespace gap
}  // namespace bluetooth
