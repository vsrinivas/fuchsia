// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test-registry.h"

int main(int argc, char** argv) {
    // TODO(gevalentino): Make it print some meaningful output.
    for (auto& test : zxtest::test::kRegisteredTests) {
        test.test_fn();
    }
    return 0;
}
