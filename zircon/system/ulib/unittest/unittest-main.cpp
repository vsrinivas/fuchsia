// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

// The library provides a default main function so test programs don't need a
// boilerplate main.  The main function is not special to the linker, so it can
// come from a library like any other.  Naturally, if the main program provides
// its own main function, the library's function will be ignored.

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
