// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_ZXIO_TESTS_FILE_TEST_SUITE_H_
#define ZIRCON_SYSTEM_ULIB_ZXIO_TESTS_FILE_TEST_SUITE_H_

#include <lib/zxio/zxio.h>

namespace FileTestSuite {

// Common test assertions for fuchsia.io/File and fuchsia.io2/File.

// When you add a new function to this list, be sure to call it from both the
// io1 and io2 test suites.
void ReadWrite(zxio_t* io);

}  // namespace FileTestSuite

#endif  // ZIRCON_SYSTEM_ULIB_ZXIO_TESTS_FILE_TEST_SUITE_H_
