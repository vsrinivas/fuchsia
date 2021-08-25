// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/integration_tests/sandbox/namespace_test.h"

TEST_F(NamespaceTest, HasDeprecatedGlobalDev) {
  ExpectExists("/dev");
  // Expect some paths under /dev that it's safe to assume would be present
  ExpectExists("/dev/class");
  ExpectExists("/dev/diagnostics");
  ExpectExists("/dev/sys");
}
