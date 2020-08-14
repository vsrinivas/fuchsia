// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>

#include "src/sys/appmgr/integration_tests/sandbox/namespace_test.h"

namespace fio = ::llcpp::fuchsia::io;

TEST_F(NamespaceTest, HasShellCommands) {
  // TODO(fxbug.dev/37858): pkgfs/thinfs do not properly support hierarchical directory rights so
  // the StrictRights test fails, switch to that once fixed
  ExpectExists("/bin");
  ExpectPathSupportsRights("/bin", fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE);
}
