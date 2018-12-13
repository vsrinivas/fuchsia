// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_registry.h"

int main(int argc, char** argv) {
    zxtest::test::TestRun();
    zxtest::test::TestRunFailure();
    zxtest::test::TestSetUpFailure();
    return 0;
}
