// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <random>
#include <vector>

#include <fbl/ref_ptr.h>
#include <fbl/string_printf.h>
#include <lib/fxl/strings/string_printf.h>
#include <perftest/perftest.h>
#include <zircon/syscalls.h>

#include "lib/component/cpp/exposed_object.h"

namespace {

const char kValue[] = "value";

class Item : public component::ExposedObject {
 public:
  Item() : ExposedObject(UniqueName("item-")) {
    object_dir().set_metric(kValue, component::IntMetric(0));
  }
  void increment() { object_dir().add_metric(kValue, 1); }
};

// Measure the time taken to increment an IntMetric.
bool TestIncrement(perftest::RepeatState* state) {
  Item* item = new Item();
  ZX_ASSERT(item != nullptr);
  while (state->KeepRunning()) {
    item->increment();
  }
  return true;
}

void RegisterTests() {
  perftest::RegisterTest("Inspect/Increment", TestIncrement);
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
