// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>

#include "src/sys/appmgr/integration_tests/sandbox/namespace_test.h"

namespace fio = fuchsia_io;

TEST_F(NamespaceTest, HasPkg) {
  ExpectExists("/pkg");

  ExpectPathSupportsStrictRights(
      "/pkg", fio::wire::OpenFlags::kRightReadable | fio::wire::OpenFlags::kRightExecutable);
  ExpectPathSupportsStrictRights(
      "/pkg/bin/has_pkg",
      fio::wire::OpenFlags::kRightReadable | fio::wire::OpenFlags::kRightExecutable,
      /*require_access_denied=*/false);
}
