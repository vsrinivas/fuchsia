// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/integration_tests/sandbox/namespace_test.h"

TEST_F(NamespaceTest, NoDurableFolder) { ExpectDoesNotExist("/durable/"); }
