// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/runtime_metadata.h"

#include "gtest/gtest.h"

namespace component {
namespace {

TEST(RuntimeMetadata, Parse) {
  RuntimeMetadata runtime;
  EXPECT_FALSE(runtime.Parse(""));
  EXPECT_FALSE(runtime.Parse("{}"));

  EXPECT_FALSE(runtime.Parse(R"JSON({ "runner": 10 })JSON"));
  EXPECT_FALSE(runtime.Parse(R"JSON({ "runner": {} })JSON"));

  EXPECT_TRUE(runtime.Parse(R"JSON({ "runner": "dart_runner" })JSON"));
  EXPECT_EQ("dart_runner", runtime.runner());
}

}  // namespace
}  // namespace component
