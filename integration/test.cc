// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/integration/test.h"

#include <thread>

#include "lib/mtl/tasks/message_loop.h"

using namespace maxwell;

void Yield() {
  // To sleep successfully we need to both yield the thread and process Mojo
  // messages.
  //
  // If we don't yield the thread, other processes run extremely slowly (for
  // example, each dependency may take about 5 seconds to start up). Yielding
  // immediately with 0 is not sufficient to remedy this.
  //
  // If we don't run the message loop, we never receive IPCs.
  std::this_thread::sleep_for(1ns);
  mtl::MessageLoop::GetCurrent()->PostQuitTask();
  mtl::MessageLoop::GetCurrent()->Run();
}

Predicate operator&&(const Predicate& a, const Predicate& b) {
  return [&a, &b] { return a() && b(); };
}

Predicate operator||(const Predicate& a, const Predicate& b) {
  return [&a, &b] { return a() || b(); };
}

Predicate operator!(const Predicate& a) {
  return [&a] { return !a(); };
}

void Sleep() {
  Sleep(1s);
}
