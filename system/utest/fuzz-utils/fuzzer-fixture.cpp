// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include "fuzzer-fixture.h"

namespace fuzzing {
namespace testing {

// Public methods

bool FuzzerFixture::CreateZircon() {
    BEGIN_HELPER;
    ASSERT_TRUE(Fixture::Create());

    END_HELPER;
}

} // namespace testing
} // namespace fuzzing
