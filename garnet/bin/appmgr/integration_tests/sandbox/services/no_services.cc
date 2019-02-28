// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/integration_tests/sandbox/namespace_test.h"

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/files/directory.h"

TEST_F(NamespaceTest, NoServices) {
  // readdir should list services in sandbox.
  std::vector<std::string> files;
  ASSERT_TRUE(files::ReadDirContents("/svc", &files));
  EXPECT_THAT(files, ::testing::UnorderedElementsAre("."));
}
