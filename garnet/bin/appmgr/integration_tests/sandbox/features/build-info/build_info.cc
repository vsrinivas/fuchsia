// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/integration_tests/sandbox/namespace_test.h"

TEST_F(NamespaceTest, BuildInfo) {
  ExpectExists("/config/build-info/product");
  ExpectExists("/config/build-info/board");
  ExpectExists("/config/build-info/version");
  ExpectExists("/config/build-info/latest-commit-date");
  ExpectExists("/config/build-info/snapshot");
}
