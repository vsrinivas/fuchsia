// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>

#include "src/sys/appmgr/integration_tests/sandbox/namespace_test.h"

namespace fio = ::fuchsia_io;

// TODO: All of the non-strict tests here are due to bugs. Once all of the bugs are fixed, we should
// switch this to always do a strict test.
static struct {
  const char *path;
  uint32_t rights;
  bool strict;
} kExpectedShellPathTestcases[] = {
    {"/boot", fio::wire::OPEN_RIGHT_READABLE | fio::wire::OPEN_RIGHT_EXECUTABLE, true},
    {"/hub", fio::wire::OPEN_RIGHT_READABLE | fio::wire::OPEN_RIGHT_WRITABLE, true},
    {"/tmp", fio::wire::OPEN_RIGHT_READABLE | fio::wire::OPEN_RIGHT_WRITABLE, true},
    {"/blob", fio::wire::OPEN_RIGHT_READABLE | fio::wire::OPEN_RIGHT_WRITABLE, true},
    {"/data", fio::wire::OPEN_RIGHT_READABLE | fio::wire::OPEN_RIGHT_WRITABLE, true},

    // TODO(fxbug.dev/45603): devfs should reject EXECUTABLE and ADMIN but doesn't, switch this to
    // strict after it's ported to ulib/fs
    {"/dev", fio::wire::OPEN_RIGHT_READABLE | fio::wire::OPEN_RIGHT_WRITABLE, false},

    // TODO(fxbug.dev/37858): pkgfs/thinfs do not properly support hierarchical directory rights so
    // the StrictRights test fails, switch to that once fixed
    {"/bin", fio::wire::OPEN_RIGHT_READABLE | fio::wire::OPEN_RIGHT_EXECUTABLE, false},
    {"/config/ssl", fio::wire::OPEN_RIGHT_READABLE, false},
    {"/pkgfs", fio::wire::OPEN_RIGHT_READABLE | fio::wire::OPEN_RIGHT_EXECUTABLE, false},
    {"/system", fio::wire::OPEN_RIGHT_READABLE | fio::wire::OPEN_RIGHT_EXECUTABLE, false},
};

TEST_F(NamespaceTest, HasShell) {
  for (auto testcase : kExpectedShellPathTestcases) {
    ExpectExists(testcase.path);
    if (testcase.strict) {
      ExpectPathSupportsStrictRights(testcase.path, testcase.rights);
    } else {
      ExpectPathSupportsRights(testcase.path, testcase.rights);
    }
  }
}
