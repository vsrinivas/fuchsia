// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>

#include "src/sys/appmgr/integration_tests/sandbox/namespace_test.h"

// Verifies that the path does not exist in the namespace.
TEST_F(NamespaceTest, PathTraversalEscapeFails) {
  ExpectDoesNotExist("/boot");
  ExpectDoesNotExist("/pkgfs/../boot");
}
