// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuzz-utils/string-map.h>
#include <stdint.h>

#include "fixture.h"

namespace fuzzing {
namespace testing {

// |fuzzing::testing::FuzzerFixture| is a fixture that understands fuzzer path locations.  It should
// not be instantiated directly; use |CreateZircon| below.
class FuzzerFixture final : public Fixture {
public:
    // Creates a number of directories and files to mimic a deployment of fuzz-targets on Zircon.
    bool CreateZircon();
};

} // namespace testing
} // namespace fuzzing
