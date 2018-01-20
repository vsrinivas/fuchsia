// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/test/loop_config.h"

namespace fidl {

const async_loop_config_t kTestLoopConfig = {
  .make_default_for_current_thread = true
};

}  // namespace fidl
