// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "environment_status.h"

#include <cstdlib>

namespace analytics::core_dev_tools {

bool IsRunByBot() { return std::getenv("SWARMING_BOT_ID"); }

}  // namespace analytics::core_dev_tools
