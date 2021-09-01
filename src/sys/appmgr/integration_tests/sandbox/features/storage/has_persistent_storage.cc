// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>

#include "src/sys/appmgr/integration_tests/sandbox/namespace_test.h"

namespace fio = fuchsia_io;

TEST_F(NamespaceTest, HasPersistentStorage) {
  ExpectExists("/data");
  ExpectPathSupportsStrictRights("/data",
                                 fio::wire::kOpenRightReadable | fio::wire::kOpenRightWritable);
}
