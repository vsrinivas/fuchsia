// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/integration_tests/sandbox/namespace_test.h"

TEST_F(NamespaceTest, HasShell) {
  // Some directories that only shell can access are present
  for (auto path : {"/boot/", "/system/", "/hub/", "/pkgfs/", "/config/ssl/"}) {
    ExpectExists(path);
  }
}
