// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/create/goldens/my-component-v2-cpp/my_component_v2_cpp.h"

#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>

#include <iostream>

namespace my_component_v2_cpp {

App::App(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {
  async::PostTask(dispatcher_, []() { std::cout << "Hello, Fuchsia!" << std::endl; });
}

}  // namespace my_component_v2_cpp
