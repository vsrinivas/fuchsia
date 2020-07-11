// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_CREATE_GOLDENS_MY_COMPONENT_V1_CPP_MY_COMPONENT_V1_CPP_H_
#define TOOLS_CREATE_GOLDENS_MY_COMPONENT_V1_CPP_MY_COMPONENT_V1_CPP_H_

#include <lib/async/dispatcher.h>

namespace my_component_v1_cpp {

// This is the component's main class. It holds all of the component's state.
class App {
 public:
  explicit App(async_dispatcher_t* dispatcher);

  // App objects cannot be copied; they are move-only.
  App(const App&) = delete;
  App& operator=(const App&) = delete;

 private:
  // |dispatcher_|, typically created from an async::Loop and bound to a thread, is used by
  // to register and wait for events. FIDL bindings use a dispatcher to listen for incoming
  // messages and dispatch them to an implementation.
  async_dispatcher_t* dispatcher_;
};

}  // namespace my_component_v1_cpp

#endif  // TOOLS_CREATE_GOLDENS_MY_COMPONENT_V1_CPP_MY_COMPONENT_V1_CPP_H_
