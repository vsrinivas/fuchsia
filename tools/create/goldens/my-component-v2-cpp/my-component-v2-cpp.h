// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_CREATE_GOLDENS_MY_COMPONENT_V2_CPP_MY_COMPONENT_V2_CPP_H_
#define TOOLS_CREATE_GOLDENS_MY_COMPONENT_V2_CPP_MY_COMPONENT_V2_CPP_H_

#include <lib/async/dispatcher.h>

namespace my_component_v2_cpp {

// This is the component's main class. It holds all of the component's state.
class App {
 public:
  explicit App(async_dispatcher_t* dispatcher);

 private:
  async_dispatcher_t* dispatcher_;
};

}  // namespace my_component_v2_cpp

#endif  // TOOLS_CREATE_GOLDENS_MY_COMPONENT_V2_CPP_MY_COMPONENT_V2_CPP_H_
