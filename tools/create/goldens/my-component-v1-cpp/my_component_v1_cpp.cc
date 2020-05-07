// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/create/goldens/my-component-v1-cpp/my_component_v1_cpp.h"

#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>

namespace my_component_v1_cpp {

App::App(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {
  async::PostTask(dispatcher_, [] { FX_LOGS(INFO) << "Hello, Fuchsia! I'm my_component_v1_cpp."; });
}

}  // namespace my_component_v1_cpp
