// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>

#include "src/sys/appmgr/integration_tests/sandbox/namespace_test.h"

namespace fio = ::llcpp::fuchsia::io;

TEST_F(NamespaceTest, HasPkg) {
  ExpectExists("/pkg");

  // TODO(fxbug.dev/37858): pkgfs/thinfs do not properly support hierarchical directory rights so
  // the StrictRights test fails on the directory, switch to that once fixed. The file test still
  // should succeed, although it returns NOT_SUPPORTED instead of ACCESS_DENIED because of pkgfs
  // differences.
  ExpectPathSupportsRights("/pkg", fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE);
  ExpectPathSupportsStrictRights("/pkg/test/has_pkg",
                                 fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
                                 /*require_access_denied=*/false);
}
