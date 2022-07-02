// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/maybe-standalone-test/maybe-standalone.h>
#include <lib/standalone-test/standalone.h>
#include <lib/zx/resource.h>

// Redeclare the standalone-test function as weak here.
[[gnu::weak]] decltype(standalone::GetBootOptions) standalone::GetBootOptions;

namespace maybe_standalone {

const BootOptions* GetBootOptions() {
  if (!standalone::GetBootOptions) {
    return nullptr;
  }
  return &standalone::GetBootOptions();
}

}  // namespace maybe_standalone
