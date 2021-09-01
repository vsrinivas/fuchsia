// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>

#include "src/sys/appmgr/integration_tests/sandbox/namespace_test.h"

namespace fio = fuchsia_io;

TEST_F(NamespaceTest, HasPkg) {
  ExpectExists("/pkg");

  // TODO(fxbug.dev/37858): pkgfs/thinfs do not properly support hierarchical directory rights so
  // the StrictRights test fails on the directory, switch to that once fixed. The file test still
  // should succeed, although it returns NOT_SUPPORTED instead of ACCESS_DENIED because of pkgfs
  // differences.
  ExpectPathSupportsRights("/pkg", fio::wire::kOpenRightReadable | fio::wire::kOpenRightExecutable);
  ExpectPathSupportsStrictRights("/pkg/bin/has_pkg",
                                 fio::wire::kOpenRightReadable | fio::wire::kOpenRightExecutable,
                                 /*require_access_denied=*/false);
}
